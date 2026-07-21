/*******************************************************************************
 * Copyright (c) 2026, SoftPLC Corporation.
 *
 * Standalone, dependency-free regression test for PR #55 (MrAlaskan):
 *   "release I/O slots after failed Forward Open".
 *
 * Background: a reserved Input-Only / Listen-Only I/O connection slot is handed
 * out by ...ConnSet::Alloc(), which flips the slot's state from
 * kConnStateNonExistent to kConnStateConfiguring the moment it is allocated.
 * If the subsequent Forward Open then fails inside CipConn::Activate(), the
 * connection is never inserted into g_active_conns and (pre-#55) nothing resets
 * its state.  Because Alloc() only ever re-hands-out slots whose state is
 * kConnStateNonExistent, that slot is leaked -- consumed permanently.  A scanner
 * that retries a mis-parameterized Forward Open a few times exhausts the pool and
 * the device starts answering kConnMgrStatusTargetObjectOutOfConnections.
 *
 * PR #55 adds Clear(false) to Activate()'s failure paths, which resets the state
 * back to kConnStateNonExistent (and, on the openCommunicationChannels() path,
 * releases the already-grabbed consuming UDP socket).
 *
 * This test pins down the observable contract: after Activate() returns failure,
 * the connection must be back in kConnStateNonExistent (slot returned to the pool).
 * It drives the config-data failure path, which is entirely within the CIP layer
 * (no sockets, deterministic): a config data segment whose size mismatches the
 * fixed config assembly makes handleConfigData() -> AssemblyInstance::RecvData()
 * fail before any socket work is attempted.
 *
 * NOTE: this test FAILS against a tree without PR #55 (state is left Configuring)
 * and PASSES once the fix is present -- that is the point of a regression test.
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
#include <byte_bufs.h>
#include "../../src/enet_encap/cpf.h"


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


// Exposes the protected config_instance so the test can wire a resolved config
// assembly without running the whole VerifyForwardOpenParams() path.
class TestConn : public CipConn
{
public:
    void SetConfigInstance( CipInstance* aInstance )   { config_instance = aInstance; }
};


static void test_activate_releases_slot_on_config_failure()
{
    printf( "PR #55: Activate() returns a failed I/O slot to kConnStateNonExistent\n" );

    // A fixed config assembly with a 4-byte capacity.
    static uint8_t cfg_store[4];
    AssemblyInstance cfg( 100, ByteBuf( cfg_store, sizeof cfg_store ) );

    TestConn conn;

    // Simulate the state a freshly Alloc()'d Input-Only/Listen-Only slot is in.
    conn.SetState( kConnStateConfiguring );
    conn.SetConfigInstance( &cfg );

    // Config application path (Assembly class, instance 100) so
    // conn_path.ConfigPath().HasAny() is true and points at the config assembly.
    conn.ConnPath().app_path[0].SetClass( kCipAssemblyClass ).SetInstance( 100 );
    conn.ConnPath().AssignConfigPath( 0 );

    // A data segment of 6 bytes (3 words) that cannot fit the 4-byte config
    // assembly, so handleConfigData() -> RecvData() rejects it (fails whether the
    // connection is treated as fixed or variable size).
    conn.ConnPath().data_seg.AddWord( 0x1111 );
    conn.ConnPath().data_seg.AddWord( 0x2222 );
    conn.ConnPath().data_seg.AddWord( 0x3333 );

    // aCpf is not dereferenced on the config-data failure path (SetSessionHandle()
    // is reached only after a successful handleConfigData()), but Activate() takes
    // a non-null pointer, so hand it a minimal Cpf.
    Cpf cpf( kCpfIdNullAddress, kCpfIdUnconnectedDataItem );

    ConnMgrStatus ext = kConnMgrStatusSuccess;

    CipError result = conn.Activate( &cpf, &ext );

    // The Forward Open must be rejected for the config data reason...
    CHECK( result == kCipErrorConnectionFailure );
    CHECK( ext == kConnMgrStatusInvalidConfigurationApplicationPath );

    // ...and, the contract PR #55 restores: the slot is released back to the pool
    // (state reset to NonExistent) rather than left stranded in Configuring.
    CHECK( conn.State() == kConnStateNonExistent );
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
    test_activate_releases_slot_on_config_failure();

    printf( "%s: %d checks, %d failure(s)\n",
            g_fail ? "FAILED" : "PASSED", g_checks, g_fail );

    return g_fail ? 1 : 0;
}
