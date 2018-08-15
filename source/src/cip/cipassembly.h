/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPASSEMBLY_H_
#define CIPSTER_CIPASSEMBLY_H_

#include <typedefs.h>
#include "ciptypes.h"
#include "cipclass.h"


/**
 * Class AssemblyInstance
 * is extended from CipInstance with an extra kCipByteArray at the end.
 * That byte array has no ownership of the low level array, which for an
 * assembly is owned by the application program and passed into
 * CreateAssemblyInstance().
 */
class AssemblyInstance : public CipInstance
{
public:
    AssemblyInstance( int aInstanceId, ByteBuf aBuf );

//protected:
    ByteBuf     byte_array;
};


// public functions

/**
 * Function NofityAssemblyConnectedDataReceived
 * notifies an Assembly object that data has been received for it.
 *
 *  The data will be copied into the assembly instance's attribute 3 and
 *  the application will be informed with the IApp_after_assembly_data_received function.
 *
 *  @param aInstance the assembly instance for which the data was received
 *  @param aInput the byte data received and its length
 *  @return
 *     - kEipStatusOk the received data was okay
 *     - EIP_ERROR the received data was wrong
 */
EipStatus NotifyAssemblyConnectedDataReceived( CipInstance* aInstance, BufReader aInput );



class CipAssemblyClass : public CipClass
{
public:
    CipAssemblyClass() :
        CipClass( kCipAssemblyClass,
            "Assembly",
            MASK7( 1,2,3,4,5,6,7 ), // common class attributes mask
            2                       // class revision
            )
    {
    }

    CipError OpenConnection( ConnectionData* aConnData,
                Cpf* aCpf, ConnMgrStatus* aExtError ); // override

    static AssemblyInstance* CreateInstance( int aInstanceId, ByteBuf aBuffer );

    /**
     * Function Init
     * sets up the Assembly Class with zero instances and sets up all services.
     *
     * @return EipStatus - kEipStatusOk if assembly object was successfully
     *  created, otherwise kEipStatusError
     */
    static EipStatus Init();
};

#endif // CIPSTER_CIPASSEMBLY_H_
