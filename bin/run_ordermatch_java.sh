#!/bin/sh
# ordermatch in Java. Needs a build with -DHAVE_JAVA=ON: quickfix.jar and libquickfixjava from lib/java,
# libquickfix from lib/, as in src/java/test-java.sh. The example is compiled first, into lib/java/examples.
cd "$(dirname "$0")" || exit 1
root=$(cd .. && pwd)
lib="$root/lib/java"

javac -d "$lib/examples/ordermatch" -cp "$lib/quickfix.jar" --release 21 \
    $(find ../src/java/examples/com/dudleylane/quickfix/examples/ordermatch -name '*.java') || exit 1
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec java -cp "$lib/quickfix.jar:$lib/examples/ordermatch" -Dcom.dudleylane.quickfix.library="$lib/libquickfixjava.so" \
    com.dudleylane.quickfix.examples.ordermatch.OrderMatch cfg/ordermatch.cfg
