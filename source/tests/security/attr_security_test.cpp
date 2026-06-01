/*******************************************************************************
 * Copyright (c) 2026, SoftPLC Corporation.
 *
 * Standalone, dependency-free regression test for the CIP attribute-registration
 * API hardening (researcher issues #1 and #2).  It deliberately does NOT use the
 * CppUTest harness (which is unbuilt); it links only against the eip library and
 * reports pass/fail via the process exit code.
 *
 *   Issue #1 (byte-array length inflation): a remotely-settable kCipByteArrayLength
 *     can no longer push the backing store past its physical capacity, and a
 *     kCipByteArray data write is bounded by capacity -- both enforced by
 *     CipByteArray inside EncodeData()/DecodeData().
 *
 *   Issue #2 (type aliasing -> pointer overwrite): the typed AttributeInsert<Type>()
 *     inserters bind the C++ storage type to the CIP wire type at COMPILE time, so
 *     the alias cannot be expressed (see compile_fail_check.sh).  At registration
 *     time, the central insert funnel additionally rejects storage that overlaps an
 *     existing attribute under an incompatible CIP type -- the runtime backstop for
 *     the deprecated void-pointer / offset escape hatch, exercised here.
 ******************************************************************************/

#include <cstdio>
#include <cstdint>
#include <cstring>

#include <cipster_api.h>
#include <cipclass.h>


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


// ---- Issue #1: byte-array length cannot exceed capacity -----------------------
static void test_issue1_length()
{
    printf( "issue #1: byte-array length bounding\n" );

    uint8_t store[8];
    memset( store, 0, sizeof store );

    CipByteArray ba( store, sizeof store );     // capacity == length == 8

    // A remote length set of 0xFFFF must be rejected, leaving the view intact.
    {
        uint8_t wire[2] = { 0xff, 0xff };       // 65535, little endian
        BufReader r( wire, sizeof wire );
        int rc = DecodeData( kCipByteArrayLength, &ba, r );

        CHECK( rc < 0 );                        // rejected
        CHECK( ba.length()   == 8 );            // unchanged
        CHECK( ba.capacity() == 8 );
    }

    // A length set within capacity is accepted...
    {
        uint8_t wire[2] = { 0x04, 0x00 };       // 4
        BufReader r( wire, sizeof wire );
        int rc = DecodeData( kCipByteArrayLength, &ba, r );

        CHECK( rc == 2 );                       // consumed the u16
        CHECK( ba.length() == 4 );
    }

    // ...and can grow back toward capacity (proves capacity was not lost: there is
    // no "shrink-only" residual of the offset/registration-time approach).
    {
        uint8_t wire[2] = { 0x07, 0x00 };       // 7  (<= capacity 8)
        BufReader r( wire, sizeof wire );
        int rc = DecodeData( kCipByteArrayLength, &ba, r );

        CHECK( rc == 2 );
        CHECK( ba.length() == 7 );
    }

    // EncodeData emits exactly length() bytes (no over-read).
    {
        ba.SetLength( 5 );
        uint8_t out[32];
        BufWriter w( out, sizeof out );
        int n = EncodeData( kCipByteArray, &ba, w );

        CHECK( n == 5 );
    }

    // The length attribute serializes the current logical length.
    {
        ba.SetLength( 5 );
        uint8_t out[8];
        BufWriter w( out, sizeof out );
        int n = EncodeData( kCipByteArrayLength, &ba, w );

        CHECK( n == 2 );
        CHECK( (uint16_t)( out[0] | (out[1] << 8) ) == 5 );
    }
}


// ---- Issue #1: byte-array data write is bounded by capacity -------------------
static void test_issue1_data()
{
    printf( "issue #1: byte-array data write bounding\n" );

    uint8_t store[16];
    memset( store, 0, sizeof store );
    memset( store + 8, 0xAA, 8 );               // guard bytes past the 8-byte capacity

    CipByteArray ba( store, 8 );                // capacity 8 of a 16-byte allocation

    // Over-capacity write (16 bytes into a capacity-8 array) is rejected, untouched.
    {
        uint8_t big[16];
        for( unsigned i = 0; i < sizeof big; ++i )
            big[i] = (uint8_t) i;

        BufReader r( big, sizeof big );
        int rc = DecodeData( kCipByteArray, &ba, r );

        CHECK( rc < 0 );                        // rejected
        CHECK( store[0] == 0 );                 // nothing written
        CHECK( store[8] == 0xAA );              // guard intact (no overrun)
        CHECK( store[15] == 0xAA );
    }

    // A write within capacity succeeds and sets the logical length.
    {
        uint8_t data[5] = { 10, 11, 12, 13, 14 };
        BufReader r( data, sizeof data );
        int rc = DecodeData( kCipByteArray, &ba, r );

        CHECK( rc == 5 );
        CHECK( ba.length() == 5 );
        CHECK( store[0] == 10 && store[4] == 14 );
        CHECK( store[8] == 0xAA );              // guard still intact
    }
}


// ---- Issue #2: registration-time backstop against incompatible aliasing -------
static void test_issue2_runtime()
{
    printf( "issue #2: registration overlap guard\n" );

    // mask 0 => no standard class attributes, keeping the container minimal.
    CipClass c( 0x1234, "SecTest", 0, 1 );

    static uint8_t s[8];
    static CipByteArray ba( s, sizeof s );

    CipAttribute* data = c.AttributeInsertByteArray( CipInstance::_I, 1, &ba );
    CHECK( data != NULL );

    // The legitimate {byte array, byte-array length} pair sharing one CipByteArray
    // is permitted.
    CipAttribute* len = c.AttributeInsertByteArrayLength( CipInstance::_I, 2, &ba );
    CHECK( len != NULL );

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    // The PoC alias: bind the same storage as a scalar via the deprecated escape
    // hatch.  The overlap guard must reject it (incompatible type at same address).
    CipAttribute* alias = c.AttributeInsert( CipInstance::_I, 3, kCipUdint, (void*) &ba );
    CHECK( alias == NULL );

    // A byte array routed through the deprecated overload is refused outright
    // (the backing type is CipByteArray now; the assert is compiled out in a
    // no-traces library build, so this returns NULL rather than aborting).
    CipAttribute* bad_ba = c.AttributeInsert( CipInstance::_I, 4, kCipByteArray, (void*) &ba );
    CHECK( bad_ba == NULL );
#pragma GCC diagnostic pop

    // Same-type views of one address are harmless and allowed.
    CipClass c2( 0x1235, "SecTest2", 0, 1 );
    static uint16_t v = 0;
    CipAttribute* x = c2.AttributeInsertUint( CipInstance::_I, 1, &v );
    CipAttribute* y = c2.AttributeInsertUint( CipInstance::_I, 2, &v );
    CHECK( x != NULL );
    CHECK( y != NULL );
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
    test_issue1_length();
    test_issue1_data();
    test_issue2_runtime();

    printf( "%s: %d checks, %d failure(s)\n",
            g_fail ? "FAILED" : "PASSED", g_checks, g_fail );

    return g_fail ? 1 : 0;
}
