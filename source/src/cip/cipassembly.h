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
    friend class CipAssemblyClass;
public:
    AssemblyInstance( int aInstanceId, ByteBuf aBuf );

    unsigned SizeBytes() const      { return byte_array.size(); }
    const ByteBuf& Buffer() const   { return byte_array; }

    /**
     * Function RecvData
     * notifies an AssemblyInstance that data has been received for it.
     *
     * The data will be copied into the assembly instance's attribute 3 and
     * the application will be informed with the AfterAssemblyDataReceived() function.
     *
     * @param aConn which connection did the io connection datagram come in on?
     *  It has the scanner specific Header32Bit info in it.
     *
     * @param aInput the byte data received and its length
     * @return
     *     - kEipStatusOk the received data was okay
     *     - EIP_ERROR the received data was wrong
     */
    EipStatus RecvData( CipConn* aConn, BufReader aInput );

protected:
    ByteBuf     byte_array;
};


// public functions


class CipAssemblyClass : public CipClass
{
public:
    CipAssemblyClass();

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

protected:

    static EipStatus get_assembly_data_attr( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus set_assembly_data_attr( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );
};

#endif // CIPSTER_CIPASSEMBLY_H_
