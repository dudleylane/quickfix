#!/bin/sh
# Regenerates the Java binding (#148): QuickfixJava.cpp and QuickfixJava.h here, and the classes under
# swig/. Run from this directory, as CI does; commit what it writes. The directory is emptied first, so a
# class SWIG no longer generates does not linger.
set -e
out=swig/com/dudleylane/quickfix
rm -rf swig
mkdir -p "$out"
swig -I../C++ -D__cpp_noexcept_function_type -DHAVE_SSL=1 -DHAVE_MYSQL -DHAVE_POSTGRESQL \
    -java -c++ -package com.dudleylane.quickfix -outdir "$out" -o QuickfixJava.cpp quickfix.i
