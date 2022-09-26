#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string>
#include <thread>
#include <fstream>
#include <rapidjson/document.h>

extern "C" {
// S2OPC Headers
#include "sopc_assert.h"
#include "libs2opc_common_config.h"
#include "libs2opc_request_builder.h"
#include "libs2opc_server.h"
}

// Tested files
#include "opcua_server.h"
#include "opcua_server_tools.h"

// Fledge / tools  includes
#include "main_test_configs.h"
#include <gtest/gtest.h>
#include <plugin_api.h>
#include <logger.h>

using namespace std;
using namespace rapidjson;
using namespace s2opc_north;

///////////////////////
// helpful test macros
#define ASSERT_STR_CONTAINS(s1,s2) ASSERT_NE(s1.find(s2), string::npos);
#define ASSERT_STR_NOT_CONTAINS(s1,s2) ASSERT_EQ(s1.find(s2), string::npos);

#define WAIT_UNTIL(c, mtimeoutMs) do {\
        int maxwaitMs(mtimeoutMs);\
        do {\
            this_thread::sleep_for(chrono::milliseconds(10));\
            maxwaitMs -= 10;\
        } while (!(c) && maxwaitMs > 0);\
    } while(0)

extern "C" {
static int north_operation_event_nbCall = 0;
static int north_operation_event (
        char *operation,
        int paramCount,
        char *names[],
        char *parameters[],
        ControlDestination destination,
        ...) {

    WARNING("Received operation '%s', paramCount=%d", operation, paramCount);
    north_operation_event_nbCall++;
    return paramCount;
}
}

static inline Datapoint* createStringDatapointValue(const std::string& name,
        const string& value) {
    DatapointValue dpv(value);
    return new Datapoint(name, dpv);
}

static inline Datapoint* createIntDatapointValue(const std::string& name,
        const long value) {
    DatapointValue dpv(value);
    return new Datapoint(name, dpv);
}

///////////////////////
// This function starts a process and return the standard output result
static string launch_and_check(SOPC_tools::CStringVect& command) {
    sigset_t mask;
    sigset_t orig_mask;
    struct timespec timeout;
    pid_t pid;

    static const char* filename("./fork.log");
    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0) {
        return "sigprocmask";
    }
    timeout.tv_sec = 2;
    timeout.tv_nsec = 0;

    pid = fork();
    if (pid < 0) return "fork";

    if (pid == 0) {
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        dup2(fd, 1);  // redirect stdout
        char **args = command.vect;
        execv(args[0], args);
        throw exception(); // not reachable
    }

    do {
        if (sigtimedwait(&mask, NULL, &timeout) < 0) {
            if (errno == EINTR) {
                /* Interrupted by a signal other than SIGCHLD. */
                continue;
            }
            else if (errno == EAGAIN) {
                printf ("Timeout, killing child\n");
                kill (pid, SIGKILL);
            }
            else {
                return "sigtimedwait";
            }
        }

        break;
    } while (1);

    int result = -1;
    waitpid(pid, &result, 0);

    std::ifstream ifs(filename);
    std::string content( (std::istreambuf_iterator<char>(ifs) ),
                           (std::istreambuf_iterator<char>()    ) );

    if (WIFEXITED(result) == 0 || WEXITSTATUS(result) != 0) {
        std::cerr << "While executing command:" << std::endl;
        for (const std::string& sRef : command.cppVect) {
            std::cout << "'" << sRef << "' ";
        }
        cerr << endl;
        cerr << "Log was:<<<" << content << ">>>" << endl;
        return command.cppVect[0] + " has terminated with code " +
                std::to_string(WEXITSTATUS(result));
    }

    return content;
}

// Complete OPCUA_Server class to test Server updates
class OPCUA_Server_Test : public OPCUA_Server {
public:
    explicit OPCUA_Server_Test(const ConfigCategory& configData):
        OPCUA_Server(configData),
        nbResponses(0),
        nbBadResponses(0) {}

    void reset(void) {
        nbResponses = 0;
        nbBadResponses = 0;
    }
    size_t nbResponses;
    size_t nbBadResponses;
    virtual void asynchWriteResponse(const OpcUa_WriteResponse* writeResp) {
        OPCUA_Server::asynchWriteResponse(writeResp);
        if (writeResp == NULL) return;

        SOPC_StatusCode status;

        DEBUG("asynchWriteResponse : %u updates", writeResp->NoOfResults);
        for (int32_t i = 0 ; i < writeResp->NoOfResults; i++) {
            status = writeResp->Results[i];
            if (status != 0) {
                WARNING("Internal data update[%d] failed with code 0x%08X", i, status);
                nbBadResponses++;
            }
            nbResponses++;
        }
    }

    std::vector<string> readResults;
    virtual void asynchReadResponse(const OpcUa_ReadResponse* readResp) {
        OPCUA_Server::asynchReadResponse(readResp);

        SOPC_StatusCode status;
        if (readResp == NULL) return;
        for (int32_t i = 0 ; i < readResp->NoOfResults; i++) {
            const SOPC_DataValue& result(readResp->Results[i]);
            char quality[4 + 8 + 4 +1];
            sprintf(quality, "Q=0x%08X,V=", result.Status);
            DEBUG("asynchReadResponse : type %d, status 0x%08X ", result.Value.BuiltInTypeId,
                    result.Status);
            string value("?");
            if (result.Value.BuiltInTypeId == SOPC_String_Id) {
                value = SOPC_String_GetRawCString(&result.Value.Value.String);
            } else  if (result.Value.BuiltInTypeId == SOPC_Byte_Id) {
                value = std::to_string(result.Value.Value.Byte);
            } else  if (result.Value.BuiltInTypeId == SOPC_Boolean_Id) {
                value =std::to_string(result.Value.Value.Boolean);
            } else {
                value =string("Unsupported type: typeId=") +
                        std::to_string(result.Value.BuiltInTypeId);
            }

            readResults.push_back(string(quality) + value);
        }
    }
};

TEST(S2OPCUA, OPCUA_Server) {
    CATCH_C_ASSERTS;
    north_operation_event_nbCall = 0;

    const SOPC_Toolkit_Build_Info buildInfo(SOPC_CommonHelper_GetBuildInfo());
    Logger::getLogger()->info("Common build date: %s", LOGGABLE(buildInfo.commonBuildInfo.buildBuildDate));
    Logger::getLogger()->info("Common build dock: %s", LOGGABLE(buildInfo.commonBuildInfo.buildDockerId));
    Logger::getLogger()->info("Common build sha1: %s", LOGGABLE(buildInfo.commonBuildInfo.buildSrcCommit));
    Logger::getLogger()->info("Common build vers: %s", LOGGABLE(buildInfo.commonBuildInfo.buildVersion));

    Logger::getLogger()->info("Server build date: %s", LOGGABLE(buildInfo.clientServerBuildInfo.buildBuildDate));
    Logger::getLogger()->info("Server build dock: %s", LOGGABLE(buildInfo.clientServerBuildInfo.buildDockerId));
    Logger::getLogger()->info("Server build sha1: %s", LOGGABLE(buildInfo.clientServerBuildInfo.buildSrcCommit));
    Logger::getLogger()->info("Server build vers: %s", LOGGABLE(buildInfo.clientServerBuildInfo.buildVersion));

    ConfigCategory testConf;
    testConf.addItem("logging", "Configure S2OPC logging level", "Info",
            "Info", {"None", "Error", "Warning", "Info", "Debug"});
    testConf.addItem("exchanged_data", "exchanged_data", "JSON", config_exData,
            config_exData);
    testConf.addItem("protocol_stack", "protocol_stack", "JSON", protocolJsonOK,
            protocolJsonOK);
    WARNING("***************JCH WIP");
    OPCUA_Server_Test server(testConf);

    Readings readings;
    // Create READING 1
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_dps"));
        dp_vect->push_back(createStringDatapointValue("do_nodeid", "ns=1;s=/label1/addr1"));
        dp_vect->push_back(createIntDatapointValue("do_value", 17));
        dp_vect->push_back(createIntDatapointValue("do_quality", 0x80000000));
        dp_vect->push_back(createIntDatapointValue("do_ts", 42));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading1", new Datapoint("data_object", do_1)));
    }

    // Create READING 2
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_sps"));
        dp_vect->push_back(createStringDatapointValue("do_nodeid", "ns=1;s=/label2/addr2"));
        dp_vect->push_back(createIntDatapointValue("do_value", 0));
        dp_vect->push_back(createStringDatapointValue("do_quality", "0x1234"));
        dp_vect->push_back(createIntDatapointValue("do_ts", 42));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading2", new Datapoint("data_object", do_1)));
    }

    server.reset();
    // Send READINGs
    server.send(readings);
    this_thread::sleep_for(chrono::milliseconds(10));

    // Read back values from server
    ASSERT_EQ(server.nbBadResponses, 0);
    ASSERT_EQ(server.nbResponses, 2);

    {
        SOPC_ReturnStatus status;
        OpcUa_ReadRequest* req(SOPC_ReadRequest_Create(2, OpcUa_TimestampsToReturn_Both));
        ASSERT_NE(nullptr, req);

        status = SOPC_ReadRequest_SetReadValueFromStrings(req, 0, "ns=1;s=/label2/addr2", SOPC_AttributeId_Value, NULL);
        ASSERT_EQ(status, SOPC_STATUS_OK);
        status = SOPC_ReadRequest_SetReadValueFromStrings(req, 1, "ns=1;s=/label1/addr1", SOPC_AttributeId_Value, NULL);
        ASSERT_EQ(status, SOPC_STATUS_OK);

        server.readResults.clear();
        server.sendAsynchRequest(req);
        ASSERT_EQ(status, SOPC_STATUS_OK);

        WAIT_UNTIL(server.readResults.size() >= 2, 1000);
        ASSERT_EQ(server.readResults.size(), 2);
        ASSERT_EQ(server.readResults[0], "Q=0x00001234,V=0");
        ASSERT_EQ(server.readResults[1], "Q=0x80000000,V=17");
    }

    // Invalid reading
    readings.clear();
    // Create READING 1
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_dps"));
        dp_vect->push_back(createStringDatapointValue("do_nodeid", "ns=1;s=/label1/addr1"));
        // ** HERE ** INVALID "do_value"
        std::vector<double> doubleVect;
        DatapointValue dpv(doubleVect);
        dp_vect->push_back(new Datapoint("do_value", dpv));
        dp_vect->push_back(createIntDatapointValue("do_quality", 0x80000000));
        dp_vect->push_back(createIntDatapointValue("do_ts", 42));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading3", new Datapoint("data_object", do_1)));

        server.reset();
        // Send READINGs
        server.send(readings);
        this_thread::sleep_for(chrono::milliseconds(10));

        // Read back values from server
        ASSERT_EQ(server.nbResponses, 1);
        ASSERT_EQ(server.nbBadResponses, 0);
    }

    // Invalid reading
    readings.clear();
    // Create READING 1
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_dps"));
        dp_vect->push_back(createStringDatapointValue("do_nodeid", "ns=1;s=/label1/addr1"));
        dp_vect->push_back(createIntDatapointValue("do_quality", 0x80000000));
        // ** HERE ** INVALID "do_value"
        dp_vect->push_back(createStringDatapointValue("do_value", "NoValue"));
        dp_vect->push_back(createIntDatapointValue("do_ts", 42));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading3", new Datapoint("data_object", do_1)));

        server.reset();
        // Send READINGs
        server.send(readings);
        this_thread::sleep_for(chrono::milliseconds(10));

        // Read back values from server
        ASSERT_EQ(server.nbResponses, 1);
        ASSERT_EQ(server.nbBadResponses, 0);
    }

    // Invalid reading
    readings.clear();
    // Create READING 1
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_dps"));
        dp_vect->push_back(createStringDatapointValue("do_nodeid", "ns=1;s=/label1/addr1"));
        dp_vect->push_back(createIntDatapointValue("do_quality", 0x80000000));
        dp_vect->push_back(createIntDatapointValue("do_value", 1));
        // ** HERE ** INVALID "do_ts"
        dp_vect->push_back(createStringDatapointValue("do_ts", "hello world!"));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading3", new Datapoint("data_object", do_1)));

        server.reset();
        // Send READINGs
        server.send(readings);
        this_thread::sleep_for(chrono::milliseconds(10));

        // Read back values from server
        ASSERT_EQ(server.nbResponses, 1);
        ASSERT_EQ(server.nbBadResponses, 0);
    }


    // Invalid reading
    readings.clear();
    // Create READING 1
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_dps"));
        dp_vect->push_back(createIntDatapointValue("do_nodeid", 84));
        dp_vect->push_back(createIntDatapointValue("do_quality", 0x80000000));
        dp_vect->push_back(createIntDatapointValue("do_value", 1));
        // ** HERE ** INVALID "do_ts"
        std::vector<double> doubleVect;
        DatapointValue dpv(doubleVect);
        dp_vect->push_back(new Datapoint("do_ts", dpv));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading3", new Datapoint("data_object", do_1)));

        server.reset();
        // Send READINGs
        server.send(readings);
        this_thread::sleep_for(chrono::milliseconds(10));

        // Read back values from server
        ASSERT_EQ(server.nbResponses, 1);
        ASSERT_EQ(server.nbBadResponses, 1);  // cannot update node "i=84"
    }

    // Invalid reading
    readings.clear();
    // Create READING 1
    {
        vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
        dp_vect->push_back(createStringDatapointValue("do_type", "opcua_dps"));
        // ** HERE ** INVALID "do_nodeid"
        std::vector<double> doubleVect;
        DatapointValue dpv(doubleVect);
        dp_vect->push_back(new Datapoint("do_nodeid", dpv));
        dp_vect->push_back(createIntDatapointValue("do_quality", 0x80000000));
        dp_vect->push_back(createIntDatapointValue("do_value", 1));
        dp_vect->push_back(createIntDatapointValue("do_ts", 42));
        DatapointValue do_1(dp_vect, true);
        readings.push_back(new Reading("reading3", new Datapoint("data_object", do_1)));

        server.reset();
        // Send READINGs
        server.send(readings);
        this_thread::sleep_for(chrono::milliseconds(10));

        // Read back values from server (No update done because NodeId is invalid)
        ASSERT_EQ(server.nbResponses, 0);
        ASSERT_EQ(server.nbBadResponses, 0);
    }

    // Cover LOG cases
    SOPC_Logger_SetTraceLogLevel(SOPC_LOG_LEVEL_DEBUG);
    SOPC_Logger_TraceError(SOPC_LOG_MODULE_CLIENTSERVER, "Demo ERROR Log");
    SOPC_Logger_TraceWarning(SOPC_LOG_MODULE_CLIENTSERVER, "Demo WARNING Log");
    SOPC_Logger_TraceInfo(SOPC_LOG_MODULE_CLIENTSERVER, "Demo INFO Log");
    SOPC_Logger_TraceDebug(SOPC_LOG_MODULE_CLIENTSERVER, "Demo DEBUG Log");
    SOPC_Logger_SetTraceLogLevel(SOPC_LOG_LEVEL_INFO);

    // Check "operation" event
    server.setpointCallbacks(north_operation_event);
    ASSERT_EQ(north_operation_event_nbCall, 0);

    ///////////////////////////////////////////
    // Use an external client to make requests
    {
        SOPC_tools::CStringVect read_cmd({"./s2opc_read",
            "-e", "opc.tcp://localhost:55345", "--encrypt",
            "-n", "i=84",
            "--username=user", "--password=password",
            "--user_policy_id=username_Basic256Sha256",
            "--client_cert=cert/client_public/client_2k_cert.der",
            "--client_key=cert/client_private/client_2k_key.pem",
            "--server_cert=cert/server_public/server_2k_cert.der",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-a", "3"});

        string execLog(launch_and_check(read_cmd));

        // cout << "EXECLOG=<" <<execLog << ">" << endl;
        ASSERT_STR_CONTAINS(execLog, "QualifiedName = 0:Root");
        ASSERT_STR_NOT_CONTAINS(execLog, "Failed session activation");
    }

    // Invalid password
    {
        SOPC_tools::CStringVect read_cmd({"./s2opc_read",
            "-e", "opc.tcp://localhost:55345", "--encrypt",
            "-n", "i=84",
            "--username=user", "--password=password2",
            "--user_policy_id=username_Basic256Sha256",
            "--client_cert=cert/client_public/client_2k_cert.der",
            "--client_key=cert/client_private/client_2k_key.pem",
            "--server_cert=cert/server_public/server_2k_cert.der",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-a", "3"});

        string execLog(launch_and_check(read_cmd));

        // cout << "EXECLOG=<" <<execLog << ">" << endl;
        ASSERT_STR_NOT_CONTAINS(execLog, "QualifiedName = 0:Root");
        ASSERT_STR_CONTAINS(execLog, "Failed session activation");
    }

    // Invalid user
    {
        SOPC_tools::CStringVect read_cmd({"./s2opc_read",
            "-e", "opc.tcp://localhost:55345", "--encrypt",
            "-n", "i=84",
            "--username=User", "--password=password",
            "--user_policy_id=username_Basic256Sha256",
            "--client_cert=cert/client_public/client_2k_cert.der",
            "--client_key=cert/client_private/client_2k_key.pem",
            "--server_cert=cert/server_public/server_2k_cert.der",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-a", "3"});

        string execLog(launch_and_check(read_cmd));

        // cout << "EXECLOG=<" <<execLog << ">" << endl;
        ASSERT_STR_NOT_CONTAINS(execLog, "QualifiedName = 0:Root");
        ASSERT_STR_CONTAINS(execLog, "Failed session activation");
    }

    // Write request to server
    // Check type SPC (BOOL)
    {
        SOPC_tools::CStringVect write_cmd({"./s2opc_write",
            "-e", "opc.tcp://localhost:55345", "--none",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-n", "ns=1;s=/labelSPC/spc",
            "-t", "1",
            "1"});

        string writeLog(launch_and_check(write_cmd));
        // cout << "WRITELOG=<" <<writeLog << ">" << endl;

        ASSERT_STR_CONTAINS(writeLog, "Write node \"ns=1;s=/labelSPC/spc\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(writeLog, "StatusCode: 0x00000000"); // OK

        SOPC_tools::CStringVect read_cmd({"./s2opc_read",
            "-e", "opc.tcp://localhost:55345", "--none",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-n", "ns=1;s=/labelSPC/spc",
            "-a", "13"});

        string readLog(launch_and_check(read_cmd));
        // cout << "READLOG=<" <<readLog << ">" << endl;
        ASSERT_STR_CONTAINS(readLog, "StatusCode: 0x00000000"); // OK
    }


    // Write request to server
    // Check type DPC (Byte)
    {
        SOPC_tools::CStringVect write_cmd({"./s2opc_write",
            "-e", "opc.tcp://localhost:55345", "--none",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-n", "ns=1;s=/labelDPC/dpc",
            "-t", "3",
            "17"});

        string writeLog(launch_and_check(write_cmd));
        // cout << "WRITELOG=<" <<writeLog << ">" << endl;

        ASSERT_STR_CONTAINS(writeLog, "Write node \"ns=1;s=/labelDPC/dpc\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(writeLog, "StatusCode: 0x00000000"); // OK

        SOPC_tools::CStringVect read_cmd({"./s2opc_read",
            "-e", "opc.tcp://localhost:55345", "--none",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-n", "ns=1;s=/labelDPC/dpc",
            "-a", "13"});

        string readLog(launch_and_check(read_cmd));
        // cout << "READLOG=<" <<readLog << ">" << endl;
        ASSERT_STR_CONTAINS(readLog, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(readLog, "Value: 17"); // Written value
    }

    // Write request to server
    // Check type MVF (float / Read only)
    {
        SOPC_tools::CStringVect write_cmd({"./s2opc_write",
            "-e", "opc.tcp://localhost:55345", "--none",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-n", "ns=1;s=/labelMVF/mvf",
            "-t", "10",
            "3.14"});

        string writeLog(launch_and_check(write_cmd));
        // cout << "WRITELOG=<" <<writeLog << ">" << endl;

        ASSERT_STR_CONTAINS(writeLog, "Write node \"ns=1;s=/labelMVF/mvf\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(writeLog, "StatusCode: 0x803B0000"); // NOde not writeable
    }

    // Check (uninitialized) Analog value
    {
        SOPC_tools::CStringVect read_cmd({"./s2opc_read",
            "-e", "opc.tcp://localhost:55345", "--none",
            "--ca=cert/trusted/cacert.der",
            "--crl=cert/revoked/cacrl.der",
            "-n", "ns=1;s=/labelMVA/mva",
            "-a", "13"});

        string readLog(launch_and_check(read_cmd));
        // cout << "READLOG=<" <<readLog << ">" << endl;
        ASSERT_STR_CONTAINS(readLog, "StatusCode: 0x80320000"); // OpcUa_BadWaitingForInitialData
    }
}

