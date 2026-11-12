#!/bin/sh
# Banzai, the Java trading client with a window. Needs a display and a build with -DHAVE_JAVA=ON:
# quickfix.jar and libquickfixjava from lib/java, libquickfix from lib/, as in src/java/test-java.sh.
# The example is compiled first, into lib/java/examples. Trade with run_ordermatch.sh or
# run_ordermatch_java.sh, or an executor.
cd "$(dirname "$0")" || exit 1
root=$(cd .. && pwd)
lib="$root/lib/java"

javac -d "$lib/examples/banzai" -cp "$lib/quickfix.jar" --release 21 \
    $(find ../src/java/examples/com/dudleylane/quickfix/examples/banzai -name '*.java') || exit 1
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec java -cp "$lib/quickfix.jar:$lib/examples/banzai" -Dcom.dudleylane.quickfix.library="$lib/libquickfixjava.so" \
    com.dudleylane.quickfix.examples.banzai.Banzai cfg/banzai.cfg
