#!/bin/bash -eu
#
# ClusterFuzzLite build script for libconfigbin
#
# Environment provided by the fuzzing infrastructure:
#   $CC / $CXX        -- clang with sanitizer flags already set
#   $CFLAGS / $CXXFLAGS
#   $LIB_FUZZING_ENGINE -- linker flag for libFuzzer
#   $OUT              -- write compiled fuzz targets here
#   $SRC              -- repo root (also the working directory)

cd "$SRC"

# Compile every library module into an object.
OBJS=""
for f in src/*.c; do
    o="$(basename "$f" .c).o"
    $CC $CFLAGS -I"$SRC/src" -c "$f" -o "$o"
    OBJS="$OBJS $o"
done

# Link each harness against the whole library.
for h in config frame overlay merge canon pipeline; do
    $CXX $CXXFLAGS $LIB_FUZZING_ENGINE \
        "fuzz/${h}_fuzzer.cc" $OBJS \
        -I"$SRC/src" \
        -o "$OUT/${h}_fuzzer"
done
