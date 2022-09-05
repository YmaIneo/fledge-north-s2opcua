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

extern "C" {
// S2OPC headers
#include "s2opc/common/sopc_common.h"
#include "s2opc/common/sopc_enums.h"
#include "s2opc/common/sopc_builtintypes.h"
#include "s2opc/common/opcua_statuscodes.h"
#include "s2opc/common/sopc_types.h"
#include "s2opc/clientserver/sopc_address_space.h"
}

// Fledge headers
#include <config_category.h>
#include <logger.h>

namespace s2opc_north
{
/**************************************************************************/
Server_AddrSpace::
Server_AddrSpace(const std::string& json)
{
#warning "TODO : fill address space!"
}

/**************************************************************************/
Server_AddrSpace::
~ Server_AddrSpace(void)
{
}

} //namespace s2opc_north

