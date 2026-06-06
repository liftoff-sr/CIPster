/*******************************************************************************
 * Copyright (c) 2026, SoftPLC Corporation.
 *
 * Regression for the CipAttribute offset-vs-pointer constructor discriminator.
 *
 * The original sanity assert masked only bits 16..31 of the cookie (0xffff0000) to
 * tell an instance offset from a real pointer.  On a 64-bit host a perfectly valid
 * pointer whose bits 16..31 happen to be zero (address & 0xffff0000 == 0) failed the
 * check and aborted at registration -- a deterministic crash once a build's layout put
 * a pointer-mode global at such an address.
 *
 * This test constructs exactly such a cookie (plus a normal small offset).  It only
 * exercises the assert when the library is built with CIPster_TRACES=ON; with traces
 * off CIPSTER_ASSERT is compiled out and the test trivially passes.  To reproduce the
 * original failure / confirm the fix, build with -DCIPster_TRACES=ON.
 ******************************************************************************/

#include <cstdio>
#include <cstdint>

#include <cipster_api.h>
#include <cipattribute.h>

// Inert application-callback stubs so the test links against eip.
EipStatus AfterAssemblyDataReceived( AssemblyInstance*, OpMode, int ) { return kEipStatusOk; }
bool      BeforeAssemblyDataSend( AssemblyInstance* )                 { return false; }
void      NotifyIoConnectionEvent( CipConn*, IoConnectionEvent )      {}
void      RunIdleChanged( uint32_t )                                  {}
void      HandleApplication()                                         {}
EipStatus ResetDevice()                                               { return kEipStatusOk; }
EipStatus ResetDeviceToInitialConfiguration( bool )                   { return kEipStatusOk; }

int main()
{
    // Pointer mode: a valid-shaped 64-bit address whose bits 16..31 are zero.  This is
    // the case that spuriously aborted under the old 0xffff0000 mask.  (Not dereferenced;
    // the constructor only stores it.)
    CipAttribute ptr_mode( 1, kCipUint, 0, 0, (uintptr_t) 0x555500001230ULL, true, false );

    // Offset mode: a normal small instance offset.
    CipAttribute off_mode( 2, kCipUint, 0, 0, (uintptr_t) 0x40, true, true );

    (void) ptr_mode;
    (void) off_mode;

    printf( "PASSED: CipAttribute accepts a valid pointer (bits 16..31 == 0) and a small offset\n" );
    return 0;
}
