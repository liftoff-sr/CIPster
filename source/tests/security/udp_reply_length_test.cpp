/*******************************************************************************
 * Copyright (c) 2026, SoftPLC Corporation.
 *
 * Standalone, dependency-free regression test for a UDP reply-length disclosure
 * reported against master 95ed840.
 *
 * Background: NetworkHandler uses one static buffer (s_buf) for both the received
 * command and the constructed reply.  Encapsulation::HandleReceivedExplicitUdpData()
 * reused its local `result`, which DeserializeEncap() had set to the header length
 * (24).  For ListServices / ListInterfaces with a non-zero Options field the command
 * handler was skipped, `result` stayed 24, and the function serialized only the
 * 24-byte header while returning 48.  The caller then sent 48 bytes, the last 24
 * of which were stale s_buf contents left by a previous (possibly different)
 * client's request.
 *
 * The fix resets `result` to "no reply" before the command switch.  This test
 * drives HandleReceivedExplicitUdpData() directly (no sockets) using a single
 * shared buffer exactly as networkhandler.cc does: it primes the buffer with a
 * long "other client" request carrying a marker, then processes a short request
 * with non-zero Options and checks that no reply is produced.  A well-formed
 * request is included as a control, verifying the returned length equals the
 * header plus the payload length declared in the serialized reply header.
 *
 * NOTE: this test FAILS against a tree without the fix (a 48-byte reply is
 * returned) and PASSES once the fix is present.
 *
 * Like its siblings it avoids the (unbuilt) CppUTest harness: it links only
 * against the eip library and reports via the process exit code.
 ******************************************************************************/

#include <cstdio>
#include <cstdint>
#include <cstring>

#include <cipster_api.h>
#include <encap.h>


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


// Shared command/reply buffer, mirroring s_buf in networkhandler.cc.
static uint8_t s_buf[CIPSTER_ETHERNET_BUFFER_SIZE];

static const char s_marker[] = "MARKER-FROM-CLIENT-P!!!!";     // 24 chars


// Write a 24-byte encapsulation header into s_buf, return its length.
static int put_header( EncapCmd aCmd, uint32_t aOptions, uint16_t aLength = 0 )
{
    BufWriter out( s_buf, sizeof s_buf );

    out.put16( aCmd )
    .put16( aLength )
    .put32( 0 )             // session handle
    .put32( 0 )             // status
    .append( (const uint8_t*) "ctx12345", 8 )
    .put32( aOptions );

    return out.data() - s_buf;
}


static int process( int aCommandLen )
{
    SockAddr from( 5002, 0x7f000001 );

    return Encapsulation::HandleReceivedExplicitUdpData( -1, from,
                BufReader( s_buf, aCommandLen ),
                BufWriter( s_buf, sizeof s_buf ), true );
}


// Prime s_buf with a 48-byte request from "client P": a header plus the marker.
static void prime_with_marker( EncapCmd aCmd )
{
    int hdrz = put_header( aCmd, 1, sizeof s_marker - 1 );
    memcpy( s_buf + hdrz, s_marker, sizeof s_marker - 1 );

    // client P's own request has non-zero Options, so it must not be answered.
    CHECK( process( hdrz + sizeof s_marker - 1 ) < 0 );
}


static void test_nonzero_options_no_stale_reply( EncapCmd aCmd, const char* aName )
{
    printf( "UDP %s with non-zero Options: no reply, no stale buffer bytes\n", aName );

    memset( s_buf, 0, sizeof s_buf );

    prime_with_marker( aCmd );

    // "client B": a bare 24-byte header with non-zero Options.
    int hdrz = put_header( aCmd, 1 );
    int replyz = process( hdrz );

    CHECK( replyz <= 0 );

    // Belt and braces: should a reply ever be produced, it must not reach the
    // residual marker bytes from the previous request.
    if( replyz > hdrz )
        CHECK( memcmp( s_buf + hdrz, s_marker, sizeof s_marker - 1 ) != 0 );
}


static void test_reply_length_matches_header( EncapCmd aCmd, const char* aName )
{
    printf( "UDP %s control: returned length == header + declared payload\n", aName );

    memset( s_buf, 0, sizeof s_buf );

    prime_with_marker( aCmd );

    int hdrz = put_header( aCmd, 0 );
    int replyz = process( hdrz );

    CHECK( replyz > ENCAPSULATION_HEADER_LENGTH );

    // reply header's Length field (bytes 2-3, little endian)
    int declared = s_buf[2] | ( s_buf[3] << 8 );

    CHECK( replyz == ENCAPSULATION_HEADER_LENGTH + declared );
}


// ---- Application callbacks the eip library expects an adapter app to provide ----
// This test is not a running adapter, so they are inert stubs that merely satisfy the
// linker.  None of them are reached by the tests above.
EipStatus AfterAssemblyDataReceived( AssemblyInstance*, OpMode, int ) { return kEipStatusOk; }
bool      BeforeAssemblyDataSend( AssemblyInstance* )                 { return false; }
void      NotifyIoConnectionEvent( CipConn*, IoConnectionEvent )      {}
void      RunIdleChanged( uint32_t )                                  {}
void      HandleApplication()                                         {}
EipStatus ResetDevice()                                               { return kEipStatusOk; }
EipStatus ResetDeviceToInitialConfiguration( bool )                   { return kEipStatusOk; }


int main()
{
    test_nonzero_options_no_stale_reply( kEncapCmdListServices,   "ListServices" );
    test_nonzero_options_no_stale_reply( kEncapCmdListInterfaces, "ListInterfaces" );

    test_reply_length_matches_header( kEncapCmdListServices,   "ListServices" );
    test_reply_length_matches_header( kEncapCmdListInterfaces, "ListInterfaces" );

    printf( "%s: %d checks, %d failure(s)\n",
            g_fail ? "FAILED" : "PASSED", g_checks, g_fail );

    return g_fail ? 1 : 0;
}
