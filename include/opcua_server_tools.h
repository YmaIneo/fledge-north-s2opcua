#ifndef  INCLUDE_OPCUA_SERVER_TOOLS_H_
#define  INCLUDE_OPCUA_SERVER_TOOLS_H_
/*
 * Fledge north service plugin
 *
 * Copyright (c) 2021 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Jeremie Chabod
 */

// System includes
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <utility>
#include <string>

// Fledge includes
#include "logger.h"
#include "rapidjson/document.h"

extern "C" {
// S2OPC Headers
#include "sopc_builtintypes.h"
#include "sopc_log_manager.h"
// From S2OPC "clientserver/frontend"
#include "libs2opc_server_config_custom.h"
};

/// Project includes

/**************************************************************************/
/*                DEFINITIONS                                             */
/**************************************************************************/
#define JSON_EXCHANGED_DATA "exchanged_data"
#define JSON_DATAPOINTS "datapoints"
#define JSON_PROTOCOLS "protocols"
#define JSON_LABEL "label"
#define JSON_PIVOT_ID "pivot_id"
#define JSON_PIVOT_TYPE "pivot_type"

#define PROTOCOL_S2OPC "opcua"
#define JSON_PROT_NAME "name"
#define JSON_PROT_ADDR "address"
#define JSON_PROT_TYPEID "typeid"

/**************************************************************************/
/*                HELPER MACROS                                           */
/**************************************************************************/

/* HELPER MACROS*/
static Logger* logger(Logger::getLogger());
#define DEBUG logger->debug
#define INFO logger->info
#define WARNING logger->warn
#define ERROR logger->error
#define FATAL logger->fatal

extern "C" {
extern void plugin_Assert_UserCallback(const char* context);
}

#define ASSERT_CONTEXT __FILE__ ":" SOPC_PP_STR(__LINE__) ":"

#ifdef UNIT_TESTING
// For unit tests, simplify macro to avoid multiple branches created by C++
#define ASSERT(c,  ...) do {if (!(c)) throw std::exception();}while(0)
#define ASSERT_NOT_NULL(c)  do {if (nullptr == (c)) throw std::exception();}while(0)
static inline const char* LOGGABLE(const std::string &s){return s.c_str();}
static inline const char* LOGGABLE(const char* s){return s;}

#else  // UNIT_TESTING not defined
// Improve SOPC_ASSERT to allow run-time elaborated messages
#define ASSERT(c,  ...) do { \
    if (!(c)) {\
        FATAL("ASSERT FAILED in " ASSERT_CONTEXT __VA_ARGS__);\
        throw std::exception();\
    }\
} while (0)

#define ASSERT_NOT_NULL(c) ASSERT((c) != NULL, "NULL pointer:'" #c "'")

// Note: it is possible (for performance reasons) to remove the logging robustness by simply
// using:
// #define LOGGABLE(s) (s).c_str()
#define LOGGABLE(s) SOPC_tools::loggableString(s).c_str()
#endif

/**************************************************************************/
/*                     FUNCTIONS                                          */
/**************************************************************************/
namespace SOPC_tools {

/** \brief this function ensures there are no non-ASCII chars sent to logger because
 * this can makes FLEDGE logger crash, and results in no log at all
 */
const std::string loggableString(const std::string& log);

/**
 * @param status a S2OPC status code
 * @return a human-readable representation of a status code
 */
const char* statusCodeToCString(const int status);

/** Return an uppercase version oof str */
std::string toUpperString(const std::string & str);

/* Basic JSON parsers with related asserts */
std::string getString(const rapidjson::Value& value,
        const char* section, const std::string& context);
std::string getString(const rapidjson::Value& value, const std::string& context);
const rapidjson::Value& getObject(const rapidjson::Value& value,
        const char* section, const std::string& context);
void checkObject(const rapidjson::Value& value, const std::string& context);
const rapidjson::Value::ConstArray getArray(const rapidjson::Value& value,
        const char* section, const std::string& context);

/**************************************************************************/
/*              SOPC CONVERSION FUNTIONS                                  */
/**************************************************************************/
/** Get a string representation of a NodeId*/
std::string toString(const SOPC_NodeId& nodeid);
SOPC_NodeId* createNodeId(const std::string& nodeid);

SOPC_Log_Level toSOPC_Log_Level(const std::string & str);
SOPC_BuiltinId toBuiltinId(const std::string& name);
bool pivotTypeToReadOnly(const std::string& pivotType);
SOPC_SecurityPolicy_URI toSecurityPolicy(const std::string& policy);
SOPC_SecurityModeMask toSecurityMode(const std::string& mode);
const OpcUa_UserTokenPolicy* toUserToken(const std::string& token);

/**************************************************************************/
/*              VECTOR MANAGEMENT (C-binding)                             */
/**************************************************************************/
/** Vector of string */
typedef std::vector<std::string> StringVect_t;
typedef std::pair<std::string, std::string> StringPair_t;
typedef std::vector<StringPair_t> StringMap_t;

/**
 * CStringVect intends at making a String vector useable by C S2OPC layer.
 * @field vect Contains a C-representation of the array, including a
 *  NULL terminating string
 * @field size The number of non-NULL elements in vect
 */
struct CStringVect {
    /**
     * Build a C-like vector using  C+ STL vector
     */
    explicit CStringVect(const StringVect_t& ref);
    explicit CStringVect(const rapidjson::Value& ref, const std::string& context);
    /** Frees vect */
    virtual ~CStringVect(void);
    /**
     * \brief Checks (using ASSERT) that all elements in vector are R-O accessible files
     */
    void checkAllFilesExist(void)const;
    size_t size;
    char** vect;
    const char** cVect;
    StringVect_t cppVect;
};

}   // namespace SOPC_tools

#endif  //   INCLUDE_OPCUA_SERVER_TOOLS_H_
