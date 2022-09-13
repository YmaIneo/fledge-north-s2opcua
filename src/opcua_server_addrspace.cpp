/*
 * Fledge Power north plugin.
 *
 * Copyright (c) 2022 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Jeremie Chabod
 */

#include "opcua_server_addrspace.h"

// System headers
#include <stdio.h>
#include <map>

extern "C" {
// S2OPC headers
#include "s2opc/common/sopc_macros.h"
#include "s2opc/common/sopc_common.h"
#include "s2opc/common/sopc_enums.h"
#include "s2opc/common/sopc_builtintypes.h"
#include "s2opc/common/opcua_statuscodes.h"
#include "s2opc/common/sopc_types.h"
#include "s2opc/clientserver/sopc_address_space.h"
}

// Fledge headers
#include "config_category.h"
#include "logger.h"

/// Project includes
#include "opcua_server_config.h"

namespace {
using std::string;

/**************************************************************************/
s2opc_north::NodeVect_t getNS0(void) {
    s2opc_north::NodeVect_t result;

    const uint32_t nbNodes(SOPC_Embedded_AddressSpace_nNodes_nano);
    SOPC_AddressSpace_Node* nodes(SOPC_Embedded_AddressSpace_Nodes_nano);

    for (uint32_t i = 0 ; i < nbNodes; i++) {
        SOPC_AddressSpace_Node* node(nodes + i);
        result.push_back(node);
    }

    return result;
}

/**************************************************************************/
static void toLocalizedText(SOPC_LocalizedText* localText, const std::string& text) {
    static const SOPC_LocalizedText emptyLocal = {{0, 0, NULL}, {0, 0, NULL}, NULL};
    *localText = emptyLocal;

    SOPC_String_InitializeFromCString(&localText->defaultText, text.c_str());
}

static string getString(const rapidjson::Value& value,
        const char* section, const std::string& context)
{
    ASSERT(value.HasMember(section), "Missing STRING '%s' in '%s'",
            section, context.c_str());
    const rapidjson::Value& object(value[section]);
    ASSERT(object.IsString(), "Error :'%s' in '%s' must be an STRING",
            section, context.c_str());
    return object.GetString();
}

static const rapidjson::Value& getObject(const rapidjson::Value& value,
        const char* section, const std::string& context)
{
    ASSERT(value.HasMember(section), "Missing OBJECT '%s' in '%s'",
            section, context.c_str());
    const rapidjson::Value& object(value[section]);
    ASSERT(object.IsObject(), "Error :'%s' in '%s' must be an OBJECT",
            section, context.c_str());
    return object;
}


static const rapidjson::Value::ConstArray getArray(const rapidjson::Value& value,
        const char* section, const std::string& context)
{
    ASSERT(value.HasMember(section), "Missing ARRAY '%s' in '%s'",
            section, context.c_str());
    const rapidjson::Value& object(value[section]);
    ASSERT(object.IsArray(), "Error :'%s' in '%s' must be an ARRAY",
            section, context.c_str());
    return object.GetArray();
}

static std::string toString(const SOPC_NodeId& nodeid) {
    char* nodeIdStr(SOPC_NodeId_ToCString(&nodeid));
    string result(nodeIdStr);
    delete nodeIdStr;
    return result;
}


template <typename T>
class GarbageCollectorC {
 public:
    typedef T* pointer;
    void reallocate(pointer& ptr, size_t oldSize, size_t newSize);

 private:
    typedef map<pointer, bool>  ptrMap;  // Note that only key is used
    ptrMap mAllocated;
};
static GarbageCollectorC<OpcUa_ReferenceNode> referencesGarbageCollector;

template<typename T>
void
GarbageCollectorC<T>::
reallocate(pointer& ptr, size_t oldSize, size_t newSize) {

    const pointer oldPtr(ptr);
    auto it = mAllocated.find(oldPtr);

    ptr = new T[newSize];
    SOPC_ASSERT(NULL != ptr);

    memcpy(ptr, oldPtr, oldSize * sizeof(T));

    WARNING("JCH TODO reallocate %p : %p [DELETE=%d ,%u elts]", oldPtr, ptr, it != mAllocated.end(), newSize);
    if (it != mAllocated.end()) {
        delete(oldPtr);
        mAllocated.erase(it);
    }
    mAllocated.insert({ptr, true});
}
}   // namespace

namespace {
static const uint16_t nameSpace0(0);
static const uint32_t serverIndex(0);
static const SOPC_String String_NULL = {0, 0, NULL};
static const SOPC_NodeId NodeId_Organizes = {SOPC_IdentifierType_Numeric, nameSpace0, 35};
static const SOPC_NodeId NodeId_HasTypeDefinition = {SOPC_IdentifierType_Numeric, nameSpace0, 40};
static const SOPC_NodeId NodeId_HasComponent = {SOPC_IdentifierType_Numeric, nameSpace0, 47};
static const SOPC_NodeId NodeId_BaseDataVariableType = {SOPC_IdentifierType_Numeric, nameSpace0, 63};
static const SOPC_NodeId NodeId_Root_Objects = {SOPC_IdentifierType_Numeric, nameSpace0, 85};

}


namespace s2opc_north {

/**************************************************************************/
CNode::
CNode(SOPC_StatusCode defaultStatusCode) {
    mNode.node_class = OpcUa_NodeClass_Unspecified;     // Filled by child classes
    mNode.value_status = defaultStatusCode;
    mNode.value_source_ts = {0, 0};
}

/**************************************************************************/
void
CNode::
insertAndCompleteReferences(NodeVect_t& nodes){
    nodes.push_back(&mNode);
    // Find references and invert them
    const SOPC_NodeId& nodeId(mNode.data.variable.NodeId);
    const uint32_t nbRef(mNode.data.variable.NoOfReferences);
    const OpcUa_ReferenceNode* ref(mNode.data.variable.References);
    for (uint32_t i = 0 ; i < nbRef; i++) {
        if (ref[i].TargetId.ServerIndex == serverIndex) {
            const SOPC_NodeId& refTargetId(ref[i].TargetId.NodeId);
            // create a reverse reference

            // Find matching node in 'nodes'
            bool found (false);
            for (SOPC_AddressSpace_Node* pNode : nodes) {
                if (NULL != pNode && SOPC_NodeId_Equal(&pNode->data.variable.NodeId, &refTargetId)){
                    // Insert space in target references
                    ASSERT(!found, "Several match for the same Node Id");
                    found = true;
                    // Initial setup provides RO-Mem allocation. Thus deallocation shall only be done for
                    // elements explicitly allocated here
                    const size_t oldSize(pNode->data.variable.NoOfReferences);
                    const size_t newSize(oldSize + 1);
                    referencesGarbageCollector.reallocate(pNode->data.variable.References,
                            oldSize, newSize);

                    // Fill new reference with inverted reference
                    OpcUa_ReferenceNode& reverse(pNode->data.variable.References[oldSize]);
                    reverse.IsInverse = not ref->IsInverse;
                    reverse.ReferenceTypeId = ref->ReferenceTypeId;
                    reverse.TargetId.NodeId = nodeId;
                    reverse.TargetId.ServerIndex = serverIndex;
                    reverse.TargetId.NamespaceUri = String_NULL;

                    pNode->data.variable.NoOfReferences = newSize;
                    WARNING("JCH DEBUG reversed reference OK for nodeId '%s' : '%s'",
                            toString(nodeId).c_str(), toString(pNode->data.variable.NodeId).c_str());
                }
            }
            if (!found) {
                WARNING("No reverse reference found for nodeId '%s'", toString(nodeId).c_str());
            }
        }
    }
}

/**************************************************************************/
CVarNode::
CVarNode(const CVarInfo& varInfo, uint32_t defVal):
CCommonVarNode(varInfo) {
    OpcUa_VariableNode& variableNode = mNode.data.variable;
    variableNode.Value.ArrayType = SOPC_VariantArrayType_SingleValue;
    variableNode.Value.BuiltInTypeId = SOPC_UInt32_Id;
    variableNode.Value.DoNotClear = true;
    variableNode.Value.Value.Uint32 = defVal;
    variableNode.DataType.IdentifierType = SOPC_IdentifierType_Numeric;
    variableNode.DataType.Namespace = 0;
    variableNode.DataType.Data.Numeric = 7;
    variableNode.ValueRank = -1;
}

/**************************************************************************/
CCommonVarNode::
CCommonVarNode(const CVarInfo& varInfo) {
    SOPC_ReturnStatus status;

    mNode.node_class = OpcUa_NodeClass_Variable;
    OpcUa_VariableNode& variableNode = mNode.data.variable;
    variableNode.NodeClass = OpcUa_NodeClass_Variable;
    variableNode.encodeableType = &OpcUa_VariableNode_EncodeableType;
    variableNode.AccessLevel = (varInfo.mReadOnly ? ReadOnlyAccess : ReadWriteAccess);
    variableNode.UserAccessLevel = 0;
    variableNode.MinimumSamplingInterval = 0.0;

    // Node Id
    status = SOPC_NodeId_InitializeFromCString(
            &variableNode.NodeId, varInfo.mNodeId.c_str(), varInfo.mNodeId.length());
    ASSERT(status == SOPC_STATUS_OK, "Invalid NodeId : %s", varInfo.mNodeId.c_str());

    // Browse name
    variableNode.BrowseName.NamespaceIndex = variableNode.NodeId.Namespace;
    SOPC_String_InitializeFromCString(&variableNode.BrowseName.Name, varInfo.mBrowseName.c_str());

    ::toLocalizedText(&variableNode.DisplayName, varInfo.mDisplayName);
    ::toLocalizedText(&variableNode.Description, varInfo.mDescription);

    variableNode.NoOfReferences = 2;
    variableNode.References = new OpcUa_ReferenceNode[variableNode.NoOfReferences];

    OpcUa_ReferenceNode* ref(variableNode.References);
    // Reference #0: Organized by Root.Objects
    ref->encodeableType = &OpcUa_ReferenceNode_EncodeableType;
    ref->ReferenceTypeId = NodeId_HasComponent;
    ref->IsInverse = true;
    ref->TargetId.NodeId = NodeId_Root_Objects;
    ref->TargetId.NamespaceUri = String_NULL;
    ref->TargetId.ServerIndex = serverIndex;
    ref++;
    // Reference #1: Has Type Definition
    ref->encodeableType = &OpcUa_ReferenceNode_EncodeableType;
    ref->ReferenceTypeId = NodeId_HasTypeDefinition;
    ref->IsInverse = false;
    ref->TargetId.NodeId = NodeId_BaseDataVariableType;
    ref->TargetId.NamespaceUri = String_NULL;
    ref->TargetId.ServerIndex = serverIndex;
#warning "TODO : add reverse references?"
}

/**************************************************************************/
Server_AddrSpace::
Server_AddrSpace(const std::string& json):
    nodes(getNS0()) {
    using rapidjson::Value;

    /* "nodes" are initially set-up with namespace 0 default nodes.
     Now this will be completed with configuration-extracted data
     */
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    ASSERT(!doc.HasParseError() && doc.HasMember(JSON_EXCHANGED_DATA),
            "Malformed JSON (section '%s')", JSON_EXCHANGED_DATA);

    const Value& exData(::getObject(doc, JSON_EXCHANGED_DATA, JSON_EXCHANGED_DATA));
    const Value::ConstArray datapoints(::getArray(exData, JSON_DATAPOINTS, JSON_EXCHANGED_DATA));

    for (const Value& datapoint : datapoints)
    {
        const string label(::getString(datapoint, JSON_LABEL, JSON_DATAPOINTS));
        DEBUG("Parsing DATAPOINT(%s)", label.c_str());
        const string pivot_id(::getString(datapoint, JSON_PIVOT_ID, JSON_DATAPOINTS));
        const string pivot_type(::getString(datapoint, JSON_PIVOT_TYPE, JSON_DATAPOINTS));
        const Value::ConstArray& protocols(::getArray(datapoint, JSON_PROTOCOLS, JSON_DATAPOINTS));

        for (const Value& protocol : protocols)
        {
            try {
                const ExchangedDataC data(protocol);
                const std::string browseName;
                const std::string displayName;
                const std::string description;
                const std::string parent;
                const bool readOnly(true);
                const uint32_t value(45);
                CVarInfo cVarInfo(data.address, browseName, displayName, description, parent, readOnly);
                CVarNode* pNode= new CVarNode(cVarInfo, value);
                WARNING("Adding node data '%s' of type '%s'", data.address.c_str(), data.typeId.c_str());
                pNode->insertAndCompleteReferences(nodes);
            }
            catch (const ExchangedDataC::NotAnS2opcInstance&) {
                // Just ignore other protcols
            }
        }
    }

#warning "TODO : Add possibility to setup nano/mbedded ns0"
#warning "TODO : fill address space!"

#warning "TODO : display, 'description', 'parent', 'value', 'type'"
}

/**************************************************************************/
Server_AddrSpace::
~Server_AddrSpace(void) {
    // Note: nodes are freed automatically (See call to ::SOPC_AddressSpace_Create)
}

}   // namespace s2opc_north

