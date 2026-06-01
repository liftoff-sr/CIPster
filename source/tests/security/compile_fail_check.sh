#!/bin/sh
#
# Verifies the issue-#2 COMPILE-TIME guarantee of the typed AttributeInsert API:
#   - binding a CipByteArray to a scalar CIP type (the aliasing PoC) must NOT compile;
#   - the correct typed call MUST compile.
#
# The CppUTest harness is unbuilt, so this is a self-contained check that needs only a
# C++ compiler and the CIPster headers (no link step).
#
# Usage:
#   compile_fail_check.sh <cipster_source_dir> <user_include_dir> [CXX]
#     <cipster_source_dir>  the "source" directory (contains src/, tests/)
#     <user_include_dir>    directory containing cipster_user_conf.h
#     [CXX]                 compiler, default g++
#
# Exit code 0 => both guarantees hold.

set -u

SRC="${1:?need cipster source dir}"
UCONF="${2:?need user include dir}"
CXX="${3:-g++}"

INC="-I$UCONF -I$SRC -I$SRC/src -I$SRC/src/cip -I$SRC/src/enet_encap -I$SRC/src/utils"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/good.cpp" <<'EOF'
#include <cipster_api.h>
#include <cipclass.h>
void f( CipClass* c, CipByteArray* ba ) {
    c->AttributeInsertByteArray( CipInstance::_I, 1, ba );   // correct: must compile
}
EOF

cat > "$TMP/bad.cpp" <<'EOF'
#include <cipster_api.h>
#include <cipclass.h>
void f( CipClass* c, CipByteArray* ba ) {
    c->AttributeInsertUdint( CipInstance::_I, 4, ba );       // alias: must NOT compile
}
EOF

rc=0

printf 'compile-fail check: correct typed call should COMPILE ... '
if $CXX -std=c++0x -c $INC "$TMP/good.cpp" -o /dev/null 2>/dev/null; then
    echo "OK"
else
    echo "FAIL (the correct call did not compile)"
    rc=1
fi

printf 'compile-fail check: CipByteArray-as-Udint alias should be REJECTED ... '
if $CXX -std=c++0x -c $INC "$TMP/bad.cpp" -o /dev/null 2>/dev/null; then
    echo "FAIL (the alias compiled -- issue #2 compile-time guarantee is broken)"
    rc=1
else
    echo "OK"
fi

[ "$rc" = 0 ] && echo "PASSED" || echo "FAILED"
exit $rc
