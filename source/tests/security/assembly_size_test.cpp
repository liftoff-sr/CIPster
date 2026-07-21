/*******************************************************************************
 * Copyright (c) 2026, SoftPLC Corporation.
 *
 * Standalone, dependency-free regression test for PR #50 (MrAlaskan):
 *   "Fix Type Confusion in CorrectSizes causing invalid T->O size".
 *
 * Background: attribute 3 of an Assembly instance stores a CipByteArray (registered
 * in CipAssemblyClass with memb_offs(byte_array) and CIP type kCipByteArray).  Before
 * the CipByteArray refactor that member was a ByteBuf, and ConnectionData::CorrectSizes()
 * read the assembly's size by reinterpret-casting the attribute-3 data pointer to
 * ByteBuf* and calling size().  The refactor changed the stored type but left that lone
 * C-style cast behind.  CipByteArray and ByteBuf have incompatible layouts:
 *
 *      ByteBuf       : { uint8_t* start; uint8_t* limit; }  size() = limit - start
 *      CipByteArray  : { uint8_t* start; uint16_t cap; uint16_t len; ... } size() = len
 *
 * so reading a CipByteArray through a ByteBuf* makes size() compute (garbage pointer) -
 * start, yielding a wildly wrong length and corrupting the I/O connection-size
 * validation.  The fix reads the size via AssemblyInstance::SizeBytes() instead.
 *
 * This test reproduces the exact CorrectSizes() access path (Attribute(3) -> Data())
 * and locks in three things:
 *   1. the supported accessor (SizeBytes()/Buffer()) reports the true length;
 *   2. attribute 3's data really is a CipByteArray, whose size() is the true length;
 *   3. the pre-#50 ByteBuf* reinterpretation does NOT yield the true length -- i.e. the
 *      two types are provably non-interchangeable, so the old cast can never silently
 *      come back as "correct".
 *
 * Like its sibling attr_security_test, it deliberately avoids the (unbuilt) CppUTest
 * harness: it links only against the eip library and reports via the exit code.
 ******************************************************************************/

#include <cstdio>
#include <cstdint>
#include <cstring>

#include <cipster_api.h>
#include <cipclass.h>
#include <cipassembly.h>
#include <byte_bufs.h>
#include <ciptypes.h>


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


static void test_correctsizes_no_type_confusion()
{
    printf( "PR #50: assembly attribute-3 size is read without ByteBuf type confusion\n" );

    static const int N = 10;
    static uint8_t   store[N];
    memset( store, 0, sizeof store );

    // Constructing the class registers attribute 3 as kCipByteArray at
    // memb_offs(byte_array); no stack init or global registry is needed.
    CipAssemblyClass clazz;

    // The class takes ownership of the instance (freed with the class), so heap
    // allocate and do not delete it here.
    AssemblyInstance* inst = new AssemblyInstance( 1, ByteBuf( store, N ) );
    CHECK( clazz.InstanceInsert( inst ) );

    // (1) The accessor CorrectSizes() now uses (the #50 fix) reports the true length.
    CHECK( inst->SizeBytes()     == (unsigned) N );
    CHECK( inst->Buffer().size() == (ssize_t)  N );

    // Reproduce the exact CorrectSizes() access path.
    CipAttribute* attribute = inst->Attribute( 3 );
    CHECK( attribute != NULL );

    if( attribute )
    {
        void* attr_data = inst->Data( attribute );

        // (2) Attribute 3's storage really is a CipByteArray, whose size() is the length.
        CHECK( ((CipByteArray*) attr_data)->size() == (ssize_t) N );

        // (3) The pre-#50 bug: the same pointer read as a ByteBuf* mis-parses the
        // layout (ByteBuf::limit overlaps CipByteArray {cap,len,...}) and does NOT
        // yield N.  This is why the ByteBuf cast must never return.
        CHECK( ((ByteBuf*) attr_data)->size() != (ssize_t) N );

        // The size CorrectSizes() computes must match the honest CipByteArray length.
        CHECK( (int) inst->SizeBytes() == (int) ((CipByteArray*) attr_data)->size() );
    }

    // A zero-length assembly (heartbeat connection) must read as size 0, not garbage.
    AssemblyInstance* hb = new AssemblyInstance( 2, ByteBuf( store, 0 ) );
    CHECK( clazz.InstanceInsert( hb ) );
    CHECK( hb->SizeBytes() == 0 );

    CipAttribute* hb_attr = hb->Attribute( 3 );
    if( hb_attr )
        CHECK( ((CipByteArray*) hb->Data( hb_attr ))->size() == 0 );
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
    test_correctsizes_no_type_confusion();

    printf( "%s: %d checks, %d failure(s)\n",
            g_fail ? "FAILED" : "PASSED", g_checks, g_fail );

    return g_fail ? 1 : 0;
}
