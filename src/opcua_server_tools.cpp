/*
 * Fledge north service plugin
 *
 * Copyright (c) 2021 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Jeremie Chabod
 */

#include "opcua_server_tools.h"

// System headers
#include <unistd.h>
#include <algorithm>
#include <string>
#include <map>
#include <exception>

// FLEDGE headers
#include "rapidjson/document.h"

extern "C" {
// S2OPC Headers
#include "sopc_assert.h"
#include "sopc_types.h"
// From S2OPC "clientserver/frontend"
#include "libs2opc_server_config_custom.h"
}

/**************************************************************************/
/**************************************************************************/
namespace SOPC_tools {
using std::string;

/**************************************************************************/
const string loggableString(const string& log) {
    // Using a static variable allows to return a reference to content, but this will be
    // overwritten by any further call.
    string str(log);
    // Remmove chars from 0 ..31 and 128..255 (As char is signed, this is simplified in < ' ')
    str.erase(std::remove_if(str.begin(), str.end(), [](const char& c) {return c < ' ';}), str.end());
    return str;
}

/**************************************************************************/
const char* statusCodeToCString(const int code) {
#define HANDLE_CODE(x) case x: return #x
    switch (code) {
    HANDLE_CODE(SOPC_STATUS_OK);
    HANDLE_CODE(SOPC_STATUS_NOK);
    HANDLE_CODE(SOPC_STATUS_INVALID_PARAMETERS);
    HANDLE_CODE(SOPC_STATUS_INVALID_STATE);
    HANDLE_CODE(SOPC_STATUS_ENCODING_ERROR);
    HANDLE_CODE(SOPC_STATUS_WOULD_BLOCK);
    HANDLE_CODE(SOPC_STATUS_TIMEOUT);
    HANDLE_CODE(SOPC_STATUS_OUT_OF_MEMORY);
    HANDLE_CODE(SOPC_STATUS_CLOSED);
    HANDLE_CODE(SOPC_STATUS_NOT_SUPPORTED);
        default:
            return ("Invalid code");
    }
#undef HANDLE_CODE
}

/**************************************************************************/
/** \brief return an uppercase version of str */
string toUpperString(const string & str) {
    string copy(str);
    for (char& c : copy) {
        c = ::toupper(c);
    }
    return copy;
}

/**************************************************************************/
string getString(const rapidjson::Value& value,
        const char* section, const string& context) {
    ASSERT(value.HasMember(section), "Missing STRING '%s' in '%s'",
            section, context.c_str());
    const rapidjson::Value& object(value[section]);
    ASSERT(object.IsString(), "Error :'%s' in '%s' must be an STRING",
            section, context.c_str());
    return object.GetString();
}

/**************************************************************************/
string getString(const rapidjson::Value& value, const string& context) {
    ASSERT(value.IsString(), "Error : '%s' must be an STRING",
            context.c_str());
    return value.GetString();
}

/**************************************************************************/
const rapidjson::Value& getObject(const rapidjson::Value& value,
        const char* section, const string& context) {
    ASSERT(value.HasMember(section), "Missing OBJECT '%s' in '%s'",
            section, context.c_str());
    const rapidjson::Value& object(value[section]);
    ASSERT(object.IsObject(), "Error :'%s' in '%s' must be an OBJECT",
            section, context.c_str());
    return object;
}

/**************************************************************************/
void checkObject(const rapidjson::Value& value, const string& context) {
    ASSERT(value.IsObject(), "Error :'%s' must be an OBJECT",
            context.c_str());
}

/**************************************************************************/
const rapidjson::Value::ConstArray getArray(const rapidjson::Value& value,
        const char* section, const string& context) {
    ASSERT(value.HasMember(section), "Missing ARRAY '%s' in '%s'",
            section, context.c_str());
    const rapidjson::Value& object(value[section]);
    ASSERT(object.IsArray(), "Error :'%s' in '%s' must be an ARRAY",
            section, context.c_str());
    return object.GetArray();
}

/**************************************************************************/
string toString(const SOPC_NodeId& nodeid) {
    char* nodeIdStr(SOPC_NodeId_ToCString(&nodeid));
    string result(nodeIdStr);
    delete nodeIdStr;
    return result;
}

/**************************************************************************/
SOPC_NodeId* createNodeId(const std::string& nodeid) {
    return SOPC_NodeId_FromCString(nodeid.c_str(), nodeid.length());
}

/**************************************************************************/
SOPC_Log_Level toSOPC_Log_Level(const string & str) {
    const string sUpper(toUpperString(str));
    typedef std::pair<string, SOPC_Log_Level> Pair;
    typedef std::map<string, SOPC_Log_Level> LevelMap;
    // Note:  static_cast is only used to help editor parser.
    static const LevelMap map {
        {"DEBUG", static_cast<SOPC_Log_Level>(SOPC_LOG_LEVEL_DEBUG)},
        {"INFO", static_cast<SOPC_Log_Level>(SOPC_LOG_LEVEL_INFO)},
        {"WARNING", static_cast<SOPC_Log_Level>(SOPC_LOG_LEVEL_WARNING)},
        {"ERROR", static_cast<SOPC_Log_Level>(SOPC_LOG_LEVEL_ERROR)}
    };
    LevelMap::const_iterator it(map.find(sUpper));

    if (it != map.end()) {
        return (*it).second;
    }
    // Default value
    return SOPC_LOG_LEVEL_INFO;
}

/**************************************************************************/
SOPC_BuiltinId toBuiltinId(const string& name) {
    typedef std::pair<string, SOPC_BuiltinId> Pair;
    typedef std::map<string, SOPC_BuiltinId> TypeMap;
    static const TypeMap map {
        {"Boolean_Id", SOPC_Boolean_Id},
        {"SByte_Id", SOPC_SByte_Id},
        {"Byte_Id", SOPC_Byte_Id},
        {"Int16_Id", SOPC_Int16_Id},
        {"UInt16_Id", SOPC_UInt16_Id},
        {"Int32_Id", SOPC_Int32_Id},
        {"UInt32_Id", SOPC_UInt32_Id},
        {"Int64_Id", SOPC_Int64_Id},
        {"UInt64_Id", SOPC_UInt64_Id},
        {"Float_Id", SOPC_Float_Id},
        {"Double_Id", SOPC_Double_Id},
        {"String_Id", SOPC_String_Id},
        {"ByteString_Id", SOPC_ByteString_Id}
    };
    TypeMap::const_iterator it(map.find(name));

    if (it != map.end()) {
        return (*it).second;
    }
    ERROR("Invalid builtin type '%s'", LOGGABLE(name));
    throw std::exception();
}

/**************************************************************************/
bool pivotTypeToReadOnly(const std::string& pivotType) {
    return ((pivotType != "DpcTyp") &&
            (pivotType != "SpcTyp") &&
            (pivotType != "IncTyp") &&
            (pivotType != "ApcTyp"));
}

/**************************************************************************/
SOPC_SecurityPolicy_URI toSecurityPolicy(const string& policy) {
    typedef std::pair<string, SOPC_SecurityPolicy_URI> Pair;
    typedef std::map<string, SOPC_SecurityPolicy_URI> PolicyMap;
    static const PolicyMap map {
        {"None", SOPC_SecurityPolicy_None},
        {"Basic256", SOPC_SecurityPolicy_Basic256},
        {"Basic256Sha256", SOPC_SecurityPolicy_Basic256Sha256},
        {"Aes128Sha256RsaOaep", SOPC_SecurityPolicy_Aes128Sha256RsaOaep},
        {"Aes128Sha256RsaPss", SOPC_SecurityPolicy_Aes256Sha256RsaPss}
    };
    PolicyMap::const_iterator it(map.find(policy));

    if (it != map.end()) {
        return (*it).second;
    }
    ERROR("Invalid security policy '%s'" , LOGGABLE(policy));
    throw std::exception();
}

/**************************************************************************/
SOPC_SecurityModeMask toSecurityMode(const string& mode) {
    const string sUpper(toUpperString(mode));
    typedef std::pair<string, SOPC_SecurityModeMask> Pair;
    typedef std::map<string, SOPC_SecurityModeMask> ModeMap;
    static const ModeMap map {
        {"NONE", SOPC_SecurityModeMask_None},
        {"SIGN", SOPC_SecurityModeMask_Sign},
        {"SIGNANDENCRYPT", SOPC_SecurityModeMask_SignAndEncrypt}
    };
    ModeMap::const_iterator it(map.find(sUpper));

    if (it != map.end()) {
        return (*it).second;
    }

    ERROR("Invalid security mode: '%s'" , LOGGABLE(mode));
    throw std::exception();
}

/**************************************************************************/
/**
 * @param token the token amongst [Anonymous|UserName_None|UserName|UserName_Basic256Sha256]
 */
const OpcUa_UserTokenPolicy* toUserToken(const string& token) {
    DEBUG("Converting value '%s' to user token Id", LOGGABLE(token));
    if (token == SOPC_UserTokenPolicy_Anonymous_ID) {
        return &SOPC_UserTokenPolicy_Anonymous;
    }
    if (token == SOPC_UserTokenPolicy_UserNameNone_ID) {
        return &SOPC_UserTokenPolicy_UserName_NoneSecurityPolicy;
    }
    if (token == SOPC_UserTokenPolicy_UserName_ID) {
        return &SOPC_UserTokenPolicy_UserName_DefaultSecurityPolicy;
    }
    if (token == SOPC_UserTokenPolicy_UserNameBasic256Sha256_ID) {
        return &SOPC_UserTokenPolicy_UserName_Basic256Sha256SecurityPolicy;
    }
    return NULL;
}

/**************************************************************************/
CStringVect::
CStringVect(const StringVect_t& ref):
size(ref.size()),
vect(new char*[size + 1]),
cVect((const char**)(vect)) {
    for (size_t i=0 ; i < size; i++) {
        cppVect.push_back(ref[i]);
        vect[i] = strdup(cppVect.back().c_str());
    }
    vect[size] = NULL;
}

/**************************************************************************/
CStringVect::
CStringVect(const rapidjson::Value& ref, const std::string& context):
size(ref.GetArray().Size()),
vect(new char*[size + 1]),
cVect((const char**)(vect)) {
    size_t i(0);
    for (const rapidjson::Value& value : ref.GetArray()) {
        ASSERT(value.IsString(), "Expecting a String in array '%s'", LOGGABLE(context));
        cppVect.push_back(value.GetString());
        vect[i] = strdup(cppVect.back().c_str());
        i++;
    }
    vect[size] = NULL;
}

/**************************************************************************/
CStringVect::
~CStringVect(void) {
    for (size_t i =0 ; i < size ; i++) {
        delete vect[i];
    }
    delete vect;
}


}   // namespace SOPC_tools


