/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
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
#include <cipster_user_conf.h>
#include <cipidentity.h>
#include <cipcommon.h>
#include <cipmessagerouter.h>
#include <ciperror.h>
#include <byte_bufs.h>
#include <cipster_api.h>
#include <cipclass.h>



// attributes in CIP Identity Object, some are public so they can be examined
// when testing electronic key validity.

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

std::string product_name_ = CIPSTER_DEVICE_NAME;


void SetDeviceSerialNumber( EipUint32 serial_number )
{
    serial_number_ = serial_number;
}


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
static EipStatus reset_service( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipStatus eip_status;

    (void) instance;

    eip_status = kEipStatusOkSend;

    if( request->Data().size() == 1 )
    {
        int value = request->Data().data()[0];

        CIPSTER_TRACE_INFO( "%s: request->data_length=%d value=%d\n",
            __func__, (int) request->Data().size(), value );

        switch( value )
        {
        case 0:     // Reset type 0 -> emulate device reset / Power cycle
            if( kEipStatusOk == ResetDevice() )
            {
                // in this case there is no response since I am rebooting.
            }
            response->SetGenStatus( kCipErrorDeviceStateConflict );
            break;

        case 1:     // Reset type 1 -> reset to device settings
            if( kEipStatusOk == ResetDeviceToInitialConfiguration( true ) )
            {
                // in this case there is no response since I am rebooting.
            }
            response->SetGenStatus( kCipErrorDeviceStateConflict );
            break;

        case 2:     // Reset type 2 -> Return to factory defaults except communications parameters
            if( kEipStatusOk == ResetDeviceToInitialConfiguration( false ) )
            {
                // in this case there is no response since I am rebooting.
            }
            response->SetGenStatus( kCipErrorDeviceStateConflict );
            break;

        default:
            response->SetGenStatus( kCipErrorInvalidParameter );
            break;
        }
    }
    else if( request->Data().size() == 0 )
    {
        CIPSTER_TRACE_INFO( "%s: request->data_length=0\n", __func__ );

        // Reset type 0 -> emulate device reset / Power cycle
        if( kEipStatusOk == ResetDevice() )
        {
            // in this case there is no response since I am rebooting.
        }
        response->SetGenStatus( kCipErrorDeviceStateConflict );
    }
    else
    {
        response->SetGenStatus( kCipErrorInvalidParameter );
    }

    return eip_status;
}


static CipInstance* createIdentityInstance()
{
    CipClass* clazz = GetCipClass( kCipIdentityClass );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    i->AttributeInsert( 1, kCipUint, &vendor_id_ );
    i->AttributeInsert( 2, kCipUint, &device_type_ );
    i->AttributeInsert( 3, kCipUint, &product_code_ );
    i->AttributeInsert( 4, kCipUsintUsint, &revision_ );

    i->AttributeInsert( 5, kCipWord, &status_ );
    i->AttributeInsert( 6, kCipUdint, &serial_number_ );

    i->AttributeInsert( 7, kCipShortString, &product_name_ );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus CipIdentityInit()
{
    if( !GetCipClass( kCipIdentityClass ) )
    {
        CipClass* clazz = new CipClass(
                kCipIdentityClass,
                "Identity",                     // class name
                // Vol1 5A-2.1 says class attributes 3 thru 7 are optional.
                MASK4( 1, 2, 6, 7 ),

                // 24-Jul-2018: conformance tool whines erroneously when we
                // report kCipErrorAttributeNotSupported for GetAttributeSingle
                // against attributes 3, 4, 5.  I think this is their bug.

                1                               // class revision
                );

        // All attributes are read only, and the conformance tool wants error code
        // 0x08 not 0x14 when testing for SetAttributeSingle
        delete clazz->ServiceRemove( kSetAttributeSingle );

        RegisterCipClass( clazz );

        clazz->ServiceInsert( kReset, reset_service, "Reset" );

        createIdentityInstance();
    }

    return kEipStatusOk;
}
