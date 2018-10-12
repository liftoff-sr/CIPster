/*******************************************************************************
 * Copyright (c) 2018, SoftPLC Corporation.
 *
 *
 ******************************************************************************/

#include "ciperror.h"

// This function is in its own file so that it can be omited during linking
// if not referenced elsewhere.

const char* ExtStatusStr( ConnMgrStatus aExtStatus )
{
    const char* rs = "?";

    switch( aExtStatus )
    {
    case kConnMgrStatusSuccess:                                 rs = "success"; break;
    case kConnMgrStatusConnectionInUse:                         rs = "connection in use";   break;
    case kConnMgrStatusTransportTriggerNotSupported:            rs = "transport trigger not supported"; break;
    case kConnMgrStatusOwnershipConflict:                       rs = "ownership conflict";  break;
    case kConnMgrStatusConnectionNotFoundAtTargetApplication:   rs = "connection not found at target application";  break;
    case kConnMgrStatusInvalidNetworkConnectionParameter:       rs = "invalid network connection parameter"; break;
    case kConnMgrStatusInvalidConnectionSize:                   rs = "invalid connection size"; break;
    case kConnMgrStatusRPINotSupported:                         rs = "RPI not supported";   break;
    case kConnMgrStatusRPIValuesNotAcceptable:                  rs = "RPI value not acceptable"; break;
    case kConnMgrStatusNoMoreConnectionsAvailable:              rs = "no more connections available"; break;
    case kConnMgrStatusVendorIdOrProductcodeError:              rs = "vendor id or product code error"; break;
    case kConnMgrStatusDeviceTypeError:                         rs = "device type error"; break;
    case kConnMgrStatusRevisionMismatch:                        rs = "revision mismatch"; break;
    case kConnMgrStatusNonListenOnlyConnectionNotOpened:        rs = "non-listen only connection not opened"; break;
    case kConnMgrStatusTargetObjectOutOfConnections:            rs = "target out of connections"; break;
    case kConnMgrStatusPITGreaterThanRPI:                       rs = "PIT greater than RPI"; break;
    case kConnMgrStatusInvalidOToTConnectionType:               rs = "invalid O->T connection type"; break;
    case kConnMgrStatusInvalidTToOConnectionType:               rs = "invalid T->O connection type"; break;
    case kConnMgrStatusInvalidOToTConnectionSize:               rs = "invalid O->T connection size"; break;
    case kConnMgrStatusInvalidTToOConnectionSize:               rs = "invalid T->O connection size"; break;
    case kConnMgrStatusInvalidConfigurationApplicationPath:     rs = "invalid configuration app_path"; break;
    case kConnMgrStatusInvalidConsumingApllicationPath:         rs = "invalid consuming app_path"; break;
    case kConnMgrStatusInvalidProducingApplicationPath:         rs = "invalid producing app_path"; break;
    case kConnMgrStatusInconsistentApplicationPathCombo:        rs = "inconsisten app_path combo"; break;
    case kConnMgrStatusNullForwardOpenFunctionNotSupported:     rs = "null forward open function not supported"; break;
    case kConnMgrStatusConnectionTimeoutMultiplierNotAcceptable:rs = "connection timeout multiplier not acceptable"; break;
    case kConnMgrStatusParameterErrorInUnconnectedSendService:  rs = "parameter error in unconnected send service"; break;
    case kConnMgrStatusInvalidSegmentTypeInPath:                rs = "invalid segment type in path"; break;
    case kConnMgrStatusInForwardClosePathMismatch:              rs = "forward close path mismatch"; break;

    // default: ;  no, we want compiler warning when this switch is incomplete enumeration
    }

    return rs;
}
