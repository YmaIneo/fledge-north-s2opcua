#include <gtest/gtest.h>
#include <plugin_api.h>
#include <string.h>
#include <exception>
#include <string>
#include <rapidjson/document.h>

extern "C" {
// S2OPC Headers
#include "sopc_assert.h"
#include "sopc_log_manager.h"
}

// Tested files
#include "opcua_server_tools.h"
#include "opcua_server.h"

// Fledge / tools  includes
#include "config_category.h"
#include "main_test_configs.h"

using namespace std;
using namespace rapidjson;


extern "C" {
	PLUGIN_INFORMATION *plugin_info();

	void plugin_Assert_UserCallback(const char* context) {
	    printf("Failure context: %s\n", context ? context : nullptr);
	    assert(false);
	}
};

TEST(S2OPCUA, PluginInfo) {
    ERROR("*** TEST S2OPCUA PluginInfo");
    ASSERT_NO_C_ASSERTION;

	PLUGIN_INFORMATION *info = plugin_info();
	ASSERT_STREQ(info->name, "s2opcua");
	ASSERT_STREQ(info->type, PLUGIN_TYPE_NORTH);
}

TEST(S2OPCUA, PluginInfoConfigParse) {
    ERROR("*** TEST S2OPCUA PluginInfoConfigParse");
    ASSERT_NO_C_ASSERTION;

	PLUGIN_INFORMATION *info = plugin_info();
	Document doc;
	doc.Parse(info->config);
	ASSERT_EQ(doc.HasParseError(), false);
	ASSERT_EQ(doc.IsObject(), true);
    ASSERT_EQ(doc.HasMember("plugin"), true);
    ASSERT_EQ(doc.HasMember("protocol_stack"), true);
    ASSERT_EQ(doc.HasMember("exchanged_data"), true);
}

TEST(S2OPCUA, ServerToolsHelpers) {
    ERROR("*** TEST S2OPCUA ServerToolsHelpers");
    ASSERT_NO_C_ASSERTION;

    using namespace SOPC_tools;

    ASSERT_ANY_THROW(ASSERT(false, "Test that assert throws"));
    ASSERT_NO_THROW(ASSERT(10 + 10 < 25));

    // loggableString
    const std::string strOK1("No problem/+'9? ");
    const std::string strNOK1("No problèem/+'9? ");
    ASSERT_EQ(loggableString(strOK1), strOK1);
    ASSERT_EQ(loggableString(strNOK1), strOK1);

    // statusCodeToCString
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_OK), "SOPC_STATUS_OK");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_NOK), "SOPC_STATUS_NOK");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_INVALID_PARAMETERS), "SOPC_STATUS_INVALID_PARAMETERS");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_INVALID_STATE), "SOPC_STATUS_INVALID_STATE");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_ENCODING_ERROR), "SOPC_STATUS_ENCODING_ERROR");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_WOULD_BLOCK), "SOPC_STATUS_WOULD_BLOCK");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_TIMEOUT), "SOPC_STATUS_TIMEOUT");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_OUT_OF_MEMORY), "SOPC_STATUS_OUT_OF_MEMORY");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_CLOSED), "SOPC_STATUS_CLOSED");
    ASSERT_STREQ(statusCodeToCString(SOPC_STATUS_NOT_SUPPORTED), "SOPC_STATUS_NOT_SUPPORTED");
    ASSERT_STREQ(statusCodeToCString(-1), "Invalid code");

    // toUpperString
    ASSERT_EQ(toUpperString("Hello World!09"), "HELLO WORLD!09");

    // JSON helpers
    Document doc;
    doc.Parse("{\"a\":{\"b\":\"B\", \"c\":[\"C1\", \"C2\"]}}");
    ASSERT_EQ(doc.HasParseError(), false);
    ASSERT_EQ(doc.IsObject(), true);
    const Value& root(doc.GetObject());
    ASSERT_NO_THROW(checkObject(root, ""));

    const Value& vA(getObject(root, "a", "a"));
    ASSERT_EQ(SOPC_tools::getString(vA["b"], "b"), "B");
    ASSERT_EQ(SOPC_tools::getString(vA, "b", "a"), "B");

    const Value::ConstArray& vC(getArray(vA, "c", "c"));
    ASSERT_EQ(getString(vC[0], "c1"), "C1");
    ASSERT_EQ(getString(vC[1], "c2"), "C2");

    ASSERT_ANY_THROW(SOPC_tools::getString(root, "a", "a"));

    ASSERT_ANY_THROW(SOPC_tools::getString(root, "a"));

    ASSERT_ANY_THROW(SOPC_tools::getObject(root, "c", "c"));

    ASSERT_ANY_THROW(SOPC_tools::checkObject(vA["b"], "b"));

    ASSERT_ANY_THROW(SOPC_tools::getArray(vA ,"b", "b"));
    ASSERT_ANY_THROW(SOPC_tools::getArray(root ,"f", "f"));

    // SOPC helpers
    SOPC_NodeId* nodeId = createNodeId("ns=3;s=anything");
    ASSERT_NE(nullptr, nodeId);
    ASSERT_EQ(toString(*nodeId), "ns=3;s=anything");
    delete nodeId;

    ASSERT_EQ(toSOPC_Log_Level("DeBug"), SOPC_LOG_LEVEL_DEBUG);
    ASSERT_EQ(toSOPC_Log_Level("What"), SOPC_LOG_LEVEL_INFO);

    ASSERT_EQ(toBuiltinId("opcua_sps"), SOPC_Boolean_Id);
    ASSERT_EQ(toBuiltinId("opcua_spc"), SOPC_Boolean_Id);
    ASSERT_EQ(toBuiltinId("BadId"), SOPC_Null_Id);

    ASSERT_EQ(pivotTypeToReadOnly("opcua_spc"), false);
    ASSERT_EQ(pivotTypeToReadOnly("opcua_dpc"), false);
    ASSERT_EQ(pivotTypeToReadOnly("opcua_sps"), true);
    ASSERT_EQ(pivotTypeToReadOnly("opcua_dps"), true);

    ASSERT_EQ(toSecurityPolicy("Basic256"), SOPC_SecurityPolicy_Basic256);
    ASSERT_ANY_THROW(toSecurityPolicy("Basic266"));

    ASSERT_EQ(toSecurityMode("SiGn"), SOPC_SecurityModeMask_Sign);
    ASSERT_ANY_THROW(toSecurityMode("Basic256"));

    ASSERT_EQ(toUserToken("anonymous"), &SOPC_UserTokenPolicy_Anonymous);
    ASSERT_EQ(toUserToken("username_None"), &SOPC_UserTokenPolicy_UserName_NoneSecurityPolicy);
    ASSERT_EQ(toUserToken("username"), &SOPC_UserTokenPolicy_UserName_DefaultSecurityPolicy);
    ASSERT_EQ(toUserToken("username_Basic256Sha256"), &SOPC_UserTokenPolicy_UserName_Basic256Sha256SecurityPolicy);
    ASSERT_EQ(toUserToken("BadToken"), nullptr);
}

TEST(S2OPCUA, CStringVect) {
    ERROR("*** TEST S2OPCUA CStringVect");
    ASSERT_NO_C_ASSERTION;
    using namespace SOPC_tools;

    {
        CStringVect cVect({"str1", "str2"});
        ASSERT_EQ(cVect.size, 2);
        ASSERT_EQ(cVect.cppVect.size(), 2);
        ASSERT_EQ(cVect.cppVect[0], "str1");
        ASSERT_STREQ(cVect.cVect[0], "str1");
        ASSERT_STREQ(cVect.vect[1], "str2");
        ASSERT_STREQ(cVect.vect[2], nullptr);
    }

    {
        Document doc;
        doc.Parse("{\"a\":{\"b\":\"B\", \"c\":[\"C1\", \"C2\"]}}");
        ASSERT_EQ(doc.HasParseError(), false);
        CStringVect cVect(doc["a"]["c"], "No context");
        ASSERT_EQ(cVect.size, 2);
        ASSERT_EQ(cVect.cppVect.size(), 2);
        ASSERT_EQ(cVect.cppVect[0], "C1");
        ASSERT_STREQ(cVect.cVect[0], "C1");
        ASSERT_STREQ(cVect.vect[1], "C2");
        ASSERT_STREQ(cVect.vect[2], nullptr);
    }
}

extern "C" {
extern PLUGIN_HANDLE plugin_init(ConfigCategory *configData);
extern void plugin_shutdown(PLUGIN_HANDLE handle);
extern uint32_t plugin_send(PLUGIN_HANDLE handle, Readings& readings);
extern void plugin_register(PLUGIN_HANDLE handle,
        north_write_event_t write,
        north_operation_event_t operation);

////////////////
// event stubs
static bool gWriteEventCalled = false;
bool test_north_write_event
(char *name, char *value, ControlDestination destination, ...) {
    gWriteEventCalled = true;
    return true;
}

static int gOperEventNbCall = 0;
static string gOperEventLastOperName;
static ControlDestination gOperEventLastDestination;
static SOPC_tools::StringVect_t gOperEventLastNames;
static SOPC_tools::StringVect_t gOperEventLastParams;


int test_north_operation_event
(char *operation, int paramCount, char *names[], char *parameters[],
        ControlDestination destination, ...) {
    gOperEventNbCall++;

    // Copy parameters of last call into global test variables
    gOperEventLastOperName = operation ? operation : "NULL";
    gOperEventLastDestination = destination;

    gOperEventLastNames.clear();
    gOperEventLastParams.clear();
    for (int i(0); i < paramCount; i++) {
        gOperEventLastNames.push_back(names[i] ? names[i] : "null");
        gOperEventLastParams.push_back(parameters[i] ? parameters[i] : "null");
    }
    return paramCount;
}

int findNameInOperEvent(const std::string& name) {
    for (size_t i(0); i < gOperEventLastNames.size(); i++) {
        // std::cout<<"Name="<< gOperEventLastNames[i] << endl;
        if (name == gOperEventLastNames[i]) return i;
    }
    return -1;
}

}

TEST(S2OPCUA, PluginInstance) {
    using s2opc_north::OPCUA_Server;
    using SOPC_tools::StringVect_t;

    ERROR("*** TEST S2OPCUA PluginInstance");
    ASSERT_NO_C_ASSERTION;
    size_t idx;

    OPCUA_ClientNone client("opc.tcp://localhost:55345");
    // note : plugin_info already tested
    PLUGIN_HANDLE handle = nullptr;
    Readings readings;
    ConfigCategory config;
    config.addItem("logging", "Configure S2OPC logging level", "Info",
            "Info", {"None", "Error", "Warning", "Info", "Debug"});
    config.addItem("exchanged_data", "exchanged_data", "JSON", config_exData,
            config_exData);
    config.addItem("protocol_stack", "protocol_stack", "JSON", protocolJsonOK,
            protocolJsonOK);

    OPCUA_Server::uninitialize(); // Ensure no previous server still exists

    // Instantiate a server
    try {
        handle = plugin_init(&config);
    }
    catch (const std::exception& e) {
        cerr << "plugin_init raised an exception" << e.what() << endl;
        const int sysResult = system("tail -n 1000 /var/log/syslog |grep -i 'Fledge'");
        (void) sysResult;
        ASSERT_FALSE("plugin_init raised an exception");
    }

    ASSERT_NE(OPCUA_Server::instance(), nullptr);
    ASSERT_EQ(OPCUA_Server::instance(), (OPCUA_Server*)handle);

    // Check plugin_register
    plugin_register(handle, &test_north_write_event, &test_north_operation_event);

    gOperEventNbCall = 0;
    // Write request to server
    // Check type DPC (Byte)
    {
        string log;
        log = client.browseNode("i=85");
        ASSERT_STR_CONTAINS(log, QUOTE(i=85 -> ns=1;s=dpc "dpc"));

        log = client.browseNode("ns=1;s=dpc");
        ASSERT_STR_CONTAINS(log, QUOTE(ns=1;s=dpc -> ns=1;s=dpc/Trigger "Trigger"));

        log = client.readValue("ns=1;s=dpc/Value");
        ASSERT_STR_CONTAINS(log, "Read node \"ns=1;s=dpc/Value\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(log, "Value: 0");

        log = client.writeValue("ns=1;s=dpc/Value", SOPC_Byte_Id, "33");
        ASSERT_STR_CONTAINS(log, "Write node \"ns=1;s=dpc/Value\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK

        log = client.readValue("ns=1;s=dpc/Value");
        ASSERT_STR_CONTAINS(log, "Read node \"ns=1;s=dpc/Value\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(log, "Value: 33");
    }

    // Writing into Value does not trig any event
    ASSERT_FALSE(gWriteEventCalled);
    ASSERT_GE(gOperEventNbCall, 0);

    // Now trig the event
    {
        string log;
        log = client.writeValue("ns=1;s=dpc/Trigger", SOPC_Byte_Id, "1");
        ASSERT_STR_CONTAINS(log, "Write node \"ns=1;s=dpc/Trigger\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK

        log = client.readValue("ns=1;s=dpc/Trigger");
        ASSERT_STR_CONTAINS(log, "Read node \"ns=1;s=dpc/Trigger\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(log, "Value: 1");
        ASSERT_FALSE(gWriteEventCalled);
        ASSERT_GE(gOperEventNbCall, 1);
        ASSERT_EQ(gOperEventLastOperName, "opcua_operation");
        ASSERT_EQ(gOperEventLastDestination, DestinationBroadcast);

        StringVect_t::const_iterator it;

        ASSERT_EQ(gOperEventLastNames.size(), 6);
        ASSERT_EQ(gOperEventLastParams.size(), 6);

        idx = findNameInOperEvent("co_id");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "pivotDPC");
        idx = findNameInOperEvent("co_type");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "opcua_dpc");
        idx = findNameInOperEvent("co_value");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "33");
        idx = findNameInOperEvent("co_test");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "1");
        idx = findNameInOperEvent("co_se");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "0");
        idx = findNameInOperEvent("co_ts");
        ASSERT_NE(idx, -1);
    	const int64_t now = static_cast<int64_t>(time(NULL));
    	const int64_t rcv(std::atoi(gOperEventLastParams[idx].c_str()));
        ASSERT_TRUE(abs(now - rcv) <= 3);
    }

    gOperEventNbCall = 0;
    gOperEventNbCall = 0;
    {
        string log;
        log = client.writeValue("ns=1;s=dpc/Trigger", SOPC_Byte_Id, "2");
        ASSERT_STR_CONTAINS(log, "Write node \"ns=1;s=dpc/Trigger\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK

        log = client.readValue("ns=1;s=dpc/Trigger");
        ASSERT_STR_CONTAINS(log, "Read node \"ns=1;s=dpc/Trigger\", attribute 13:"); // Result OK, no error
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(log, "Value: 2");
        ASSERT_FALSE(gWriteEventCalled);
        ASSERT_GE(gOperEventNbCall, 1);
        ASSERT_EQ(gOperEventLastOperName, "opcua_operation");
        ASSERT_EQ(gOperEventLastDestination, DestinationBroadcast);

        StringVect_t::const_iterator it;

        ASSERT_EQ(gOperEventLastNames.size(), 6);
        ASSERT_EQ(gOperEventLastParams.size(), 6);

        idx = findNameInOperEvent("co_id");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "pivotDPC");
        idx = findNameInOperEvent("co_type");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "opcua_dpc");
        idx = findNameInOperEvent("co_value");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "33");
        idx = findNameInOperEvent("co_test");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "0");
        idx = findNameInOperEvent("co_se");
        ASSERT_NE(idx, -1);
        ASSERT_EQ(gOperEventLastParams[idx], "1");
        idx = findNameInOperEvent("co_ts");
        ASSERT_NE(idx, -1);
    	const int64_t now = static_cast<int64_t>(time(NULL));
    	const int64_t rcv(std::atoi(gOperEventLastParams[idx].c_str()));
        ASSERT_TRUE(abs(now - rcv) <= 3);
    }

    // Test "send" event
    {
        Readings readings;
        string log;

        // Check initial value
        log = client.readValue("ns=1;s=sps/Value");
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x80320000"); // OpcUa_BadWaitingForInitialData

        // Create READING 1
        {
            vector<Datapoint *>* dp_vect = new vector<Datapoint *>;
            dp_vect->push_back(createStringDatapointValue("do_type", "opcua_sps"));
            dp_vect->push_back(createStringDatapointValue("do_id", "pivotSPS"));
            dp_vect->push_back(createIntDatapointValue("do_cot", 3));
            dp_vect->push_back(createStringDatapointValue("do_source", "substituted"));
            dp_vect->push_back(createStringDatapointValue("do_comingfrom", "opcua"));
            dp_vect->push_back(createStringDatapointValue("do_ts_org", "genuine"));
            dp_vect->push_back(createStringDatapointValue("do_ts_validity", "good"));
            dp_vect->push_back(createIntDatapointValue("do_quality", 0x00000008));
            dp_vect->push_back(createIntDatapointValue("do_value", 1));
            dp_vect->push_back(createIntDatapointValue("do_value_quality", 0x00000000));
            dp_vect->push_back(createIntDatapointValue("do_ts", 1673363780));
            DatapointValue do_1(dp_vect, true);
            readings.push_back(new Reading("reading1", new Datapoint("data_object", do_1)));
        }
        plugin_send(handle, readings);
        // Wait for the request to be processed by server
        this_thread::sleep_for(chrono::milliseconds(10));

        // Read back values from server
        log = client.readValue("ns=1;s=sps/Value");
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(log, "Value: 1"); // Written value
        // Read back values from server
        log = client.readValue("ns=1;s=sps/SecondSinceEpoch");
        ASSERT_STR_CONTAINS(log, "StatusCode: 0x00000000"); // OK
        ASSERT_STR_CONTAINS(log, "Value: 1673363780"); // Written value
    }

    // Check multiple instances are not supported
    bool excpt(false);
    try {handle = plugin_init(&config);}
    catch (const std::exception&) {excpt = true;}
    ASSERT_TRUE(excpt);

    // destroy server
    plugin_shutdown(handle);
    ASSERT_EQ(OPCUA_Server::instance(), nullptr);
    plugin_shutdown(nullptr);
}


