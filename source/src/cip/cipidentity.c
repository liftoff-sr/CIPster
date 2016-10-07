/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

/**
 * @file cipidentity.c
 *
 * CIP Identity Object
 * ===================
 *
 * Implemented Attributes
 * ----------------------
 * - Attribute 1: VendorID
 * - Attribute 2: Device Type
 * - Attribute 3: Product Code
 * - Attribute 4: Revision
 * - Attribute 5: Status
 * - Attribute 6: Serial Number
 * - Attribute 7: Product Name
 *
 * Implemented Services
 * --------------------
 */

#include <string.h>
#include "opener_user_conf.h"
#include "cipidentity.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "endianconv.h"
#include "opener_api.h"

// attributes in CIP Identity Object

EipUint16 vendor_id_ = CIPSTER_DEVICE_VENDOR_ID;         //*< Attribute 1: Vendor ID

EipUint16 device_type_ = CIPSTER_DEVICE_TYPE;            //*< Attribute 2: Device Type

EipUint16 product_code_ = CIPSTER_DEVICE_PRODUCT_CODE;   //*< Attribute 3: Product Code

CipRevision revision_ =
{
    //*< Attribute 4: Revision / USINT Major, USINT Minor
    CIPSTER_DEVICE_MAJOR_REVISION,
    CIPSTER_DEVICE_MINOR_REVISION
};

EipUint16 status_ = 0;               //*< Attribute 5: Status

EipUint32 serial_number_ = 0;        //*< Attribute 6: Serial Number, has to be set prior to CIPster initialization

CipShortString product_name_ =
{
    //*< Attribute 7: Product Name
    sizeof(CIPSTER_DEVICE_NAME) - 1,
    (EipByte*) CIPSTER_DEVICE_NAME
};


/** Private functions, sets the devices serial number
 * @param serial_number The serial number of the device
 */
void SetDeviceSerialNumber( EipUint32 serial_number )
{
    serial_number_ = serial_number;
}


/** Private functions, sets the devices status
 * @param status The serial number of the deivce
 */
void SetDeviceStatus( EipUint16 status )
{
    status_ = status;
}


/** Reset service
 *
 * @param instance
 * @param request
 * @param response
 * @returns Currently always kEipOkSend is returned
 */
static EipStatus reset( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipStatus eip_status;

    (void) instance;

    eip_status = kEipStatusOkSend;

    response->reply_service = 0x80 | request->service;
    response->size_of_additional_status = 0;

    response->general_status = kCipErrorSuccess;

    if( request->data_length == 1 )
    {
        int value = request->data[0];

        CIPSTER_TRACE_INFO( "%s: request->data_length=%d value=%d\n", __func__, request->data_length, value );

        switch( value )
        {
        case 0:     // Reset type 0 -> emulate device reset / Power cycle
            if( kEipStatusOk == ResetDevice() )
            {
                // in this case there is no response since I am rebooting.
            }
            response->general_status = kCipErrorDeviceStateConflict;
            break;

        case 1:     // Reset type 1 -> reset to device settings
            if( kEipStatusOk == ResetDeviceToInitialConfiguration( true ) )
            {
                // in this case there is no response since I am rebooting.
            }
            response->general_status = kCipErrorDeviceStateConflict;
            break;

        case 2:     // Reset type 2 -> Return to factory defaults except communications parameters
            if( kEipStatusOk == ResetDeviceToInitialConfiguration( false ) )
            {
                // in this case there is no response since I am rebooting.
            }
            response->general_status = kCipErrorDeviceStateConflict;
            break;

        default:
            response->general_status = kCipErrorInvalidParameter;
            break;
        }
    }
    else if( request->data_length == 0 )
    {
        CIPSTER_TRACE_INFO( "%s: request->data_length=0\n", __func__ );

        // Reset type 0 -> emulate device reset / Power cycle
        if( kEipStatusOk == ResetDevice() )
        {
            // in this case there is no response since I am rebooting.
        }
        response->general_status = kCipErrorDeviceStateConflict;
    }
    else
    {
        response->general_status = kCipErrorInvalidParameter;
    }

    response->data_length = 0;
    return eip_status;
}


static CipInstance* createIdentityInstance()
{
    CipClass* clazz = GetCipClass( kIdentityClassCode );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    i->AttributeInsert( 1, kCipUint, kGetableSingleAndAll, GetAttrData, NULL, &vendor_id_ );
    i->AttributeInsert( 2, kCipUint, kGetableSingleAndAll, GetAttrData, NULL, &device_type_ );
    i->AttributeInsert( 3, kCipUint, kGetableSingleAndAll, GetAttrData, NULL, &product_code_ );
    i->AttributeInsert( 4, kCipUsintUsint, kGetableSingleAndAll, GetAttrData, NULL, &revision_ );

    i->AttributeInsert( 5, kCipWord, kGetableSingleAndAll, GetAttrData, NULL, &status_ );
    i->AttributeInsert( 6, kCipUdint, kGetableSingleAndAll, GetAttrData, NULL, &serial_number_ );

    i->AttributeInsert( 7, kCipShortString, kGetableSingleAndAll, GetAttrData, NULL, &product_name_ );

    clazz->InstanceInsert( i );

    return i;
}


/** @brief CIP Identity object constructor
 *
 * @returns EIP_ERROR if the class could not be created, otherwise EIP_OK
 */
EipStatus CipIdentityInit()
{
    if( !GetCipClass( kIdentityClassCode ) )
    {
        CipClass* clazz = new CipClass( kIdentityClassCode,
                "Identity",                     // class name
                (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
                MASK4( 1, 2, 6, 7 ),            // class getAttributeAll mask		CIP spec 5-2.3.2
                MASK7( 1, 2, 3, 4, 5, 6, 7 ),   // instance getAttributeAll mask	CIP spec 5-2.3.2
                1                               // class revision
                );

        RegisterCipClass( clazz );

        clazz->ServiceInsert( kReset, &reset, "Reset" );

        createIdentityInstance();
    }

    return kEipStatusOk;
}

