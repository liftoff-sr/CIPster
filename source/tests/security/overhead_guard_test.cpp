/*******************************************************************************
 * Copyright (c) 2026, SoftPLC Corporation.
 *
 * Standalone, dependency-free regression test for PR #57 (MrAlaskan):
 *   "reject connection sizes below protocol overhead".
 *
 * Background: ConnectionData::CorrectSizes() subtracts the per-packet protocol
 * overhead (2 bytes for the Class-1 sequence count, 4 bytes for the run/idle
 * 32-bit real-time header) from the requested Connection Size to obtain the
 * application payload size.  If the requested size is smaller than that overhead,
 * the subtraction underflows to a NEGATIVE payload size.  For a *variable*-size
 * (non-fixed) connection the acceptance test is only "data_size > attr_size", and
 * a negative value never exceeds the Assembly size -- so an undersized Forward
 * Open was silently accepted, leaving the negotiated size and the actual I/O
 * packet layout in disagreement.
 *
 * PR #57 computes the total overhead first and rejects the request up front when
 * the requested size cannot even cover it, with the proper CIP error code
 * (Invalid O->T / T->O Connection Size), before the subtraction.
 *
 * This test drives CorrectSizes() directly (no sockets) with a variable-size
 * connection whose requested size (3) is below the Class-1 + 32-bit-header
 * overhead (6), on both the consuming and producing sides, and checks it is
 * rejected.  A correctly-sized connection is included as a control.
 *
 * NOTE: this test FAILS against a tree without PR #57 (the undersized request is
 * accepted, so CorrectSizes() returns success) and PASSES once the fix is present
 * -- that is the point of a regression test.
 *
 * Like its siblings it avoids the (unbuilt) CppUTest harness: it links only
 * against the eip library and reports via the process exit code.
 ******************************************************************************/

#include <cstdio>
#include <cstdint>

#include <cipster_api.h>
#include <cipclass.h>
#include <cipassembly.h>
#include <cipconnection.h>


static int g_checks = 0;
static int g_fail   = 0;

#define CHECK( cond )                                                       \
    do {                                                                    \
        ++g_checks;                                                         \
        if( !(cond) ) {                                                     \
            ++g_fail;                                                       \
            printf( "  FAIL %s:%d   %s\n", __FILE__, __LINE__, #cond );     \
        }                                                                   \
    } while( 0 )


// Exposes the protected consuming/producing instance pointers so the test can
// point CorrectSizes() at real assembly instances without the full Forward Open
// path (VerifyForwardOpenParams()).
class TestConn : public CipConn
{
public:
    void SetConsumingInstance( CipInstance* aInstance )   { consuming_instance = aInstance; }
    void SetProducingInstance( CipInstance* aInstance )   { producing_instance = aInstance; }
};


static void test_correctsizes_rejects_sub_overhead()
{
    printf( "PR #57: CorrectSizes() rejects a connection size below the protocol overhead\n" );

    // Two 8-byte assemblies inserted into an Assembly class so that
    // instance->Attribute(3) resolves and SizeBytes() == 8.
    static uint8_t c_store[8];
    static uint8_t p_store[8];

    CipAssemblyClass clazz;

    AssemblyInstance* c_asm = new AssemblyInstance( 10, ByteBuf( c_store, sizeof c_store ) );
    AssemblyInstance* p_asm = new AssemblyInstance( 11, ByteBuf( p_store, sizeof p_store ) );

    CHECK( clazz.InstanceInsert( c_asm ) );
    CHECK( clazz.InstanceInsert( p_asm ) );

    // Class 1 (2-byte sequence count) + 32-bit run/idle header (4 bytes) => 6 bytes
    // of required overhead per packet.

    // --- Consuming O->T: variable connection requesting only 3 bytes (< 6) ---
    {
        TestConn conn;
        conn.SetConsumingInstance( c_asm );
        conn.Transport().SetClass( kConnTransportClass1 );
        conn.SetConsumingRTFmt( kRealTimeFmt32BitHeader );
        conn.ConsumingNCP() = NetCnParams( 3, false /*variable*/, kIOConnTypePointToPoint );

        ConnMgrStatus ext = kConnMgrStatusSuccess;
        CipError result = conn.CorrectSizes( &ext );

        CHECK( result == kCipErrorConnectionFailure );
        CHECK( ext == kConnMgrStatusInvalidOToTConnectionSize );
    }

    // --- Producing T->O: variable connection requesting only 3 bytes (< 6) ---
    // consuming_ncp stays Null (default), so only the producer side is exercised.
    {
        TestConn conn;
        conn.SetProducingInstance( p_asm );
        conn.Transport().SetClass( kConnTransportClass1 );
        conn.SetProducingRTFmt( kRealTimeFmt32BitHeader );
        conn.ProducingNCP() = NetCnParams( 3, false /*variable*/, kIOConnTypePointToPoint );

        ConnMgrStatus ext = kConnMgrStatusSuccess;
        CipError result = conn.CorrectSizes( &ext );

        CHECK( result == kCipErrorConnectionFailure );
        CHECK( ext == kConnMgrStatusInvalidTToOConnectionSize );
    }

    // --- Control: a correctly-sized consuming connection still succeeds. ---
    // 8-byte payload + 6-byte overhead = 14 requested.
    {
        TestConn conn;
        conn.SetConsumingInstance( c_asm );
        conn.Transport().SetClass( kConnTransportClass1 );
        conn.SetConsumingRTFmt( kRealTimeFmt32BitHeader );
        conn.ConsumingNCP() = NetCnParams( 14, false /*variable*/, kIOConnTypePointToPoint );

        ConnMgrStatus ext = kConnMgrStatusSuccess;
        CipError result = conn.CorrectSizes( &ext );

        CHECK( result == kCipErrorSuccess );
    }
}


// ---- Application callbacks the eip library expects an adapter app to provide ----
// This test is not a running adapter, so they are inert stubs that merely satisfy the
// linker.  None of them are reached by the test above.
EipStatus AfterAssemblyDataReceived( AssemblyInstance*, OpMode, int ) { return kEipStatusOk; }
bool      BeforeAssemblyDataSend( AssemblyInstance* )                 { return false; }
void      NotifyIoConnectionEvent( CipConn*, IoConnectionEvent )      {}
void      RunIdleChanged( uint32_t )                                  {}
void      HandleApplication()                                         {}
EipStatus ResetDevice()                                               { return kEipStatusOk; }
EipStatus ResetDeviceToInitialConfiguration( bool )                   { return kEipStatusOk; }


int main()
{
    test_correctsizes_rejects_sub_overhead();

    printf( "%s: %d checks, %d failure(s)\n",
            g_fail ? "FAILED" : "PASSED", g_checks, g_fail );

    return g_fail ? 1 : 0;
}
