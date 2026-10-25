#!/bin/sh
# Runs the Java binding tests against the CMake build, as CI does: quickfix.jar and libquickfixjava from
# lib/java, the suites under test/ compiled against the jar. Build first with -DHAVE_JAVA=ON. Works from any
# directory. Arguments name suites to run (TransportTests ...); none runs them all.
#
# The JVM runs with -Xcheck:jni, and any JNI warning it prints fails the run: a callback that leaves an
# exception pending or a local reference unfreed is a defect even when the test it ran in passed.
cd "$(dirname "$0")" || exit 1
root=$(cd ../.. && pwd)
lib="$root/lib/java"
classes="$lib/test-classes"

[ -f "$lib/quickfix.jar" ] || { echo "test-java.sh: no $lib/quickfix.jar -- build with -DHAVE_JAVA=ON"; exit 1; }
rm -rf "$classes"
javac -d "$classes" -cp "$lib/quickfix.jar" --release 21 -Xlint:all,-this-escape -Werror \
    test/com/dudleylane/quickfix/test/*.java || exit 1

export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
output=$(mktemp)
trap 'rm -f "$output"' EXIT
java -Xcheck:jni -cp "$lib/quickfix.jar:$classes" \
    -Dcom.dudleylane.quickfix.library="$lib/libquickfixjava.so" -Dquickfix.test.root="$root" \
    com.dudleylane.quickfix.test.TestRunner "$@" 2>&1 | tee "$output"
status=$(sed -n 's/^\([0-9]*\) passed, \([0-9]*\) failed$/\2/p' "$output")
if [ -z "$status" ]; then
    echo "test-java.sh: the runner did not finish"
    exit 1
fi
if grep -q 'WARNING in native method' "$output"; then
    echo "test-java.sh: -Xcheck:jni reported a JNI misuse (above)"
    exit 1
fi
[ "$status" = 0 ]
