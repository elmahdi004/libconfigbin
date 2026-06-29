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

# Compile library objects
$CC  $CFLAGS   -I"$SRC/src" -c src/configbin.c -o configbin.o
$CC  $CFLAGS   -I"$SRC/src" -c src/value.c     -o value.o
$CC  $CFLAGS   -I"$SRC/src" -c src/frame.c     -o frame.o
$CC  $CFLAGS   -I"$SRC/src" -c src/buffer.c    -o buffer.o
$CC  $CFLAGS   -I"$SRC/src" -c src/overlay.c   -o overlay.o

# Link fuzz targets
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE \
    fuzz/config_fuzzer.cc configbin.o value.o \
    -I"$SRC/src" \
    -o "$OUT/config_fuzzer"

$CXX $CXXFLAGS $LIB_FUZZING_ENGINE \
    fuzz/frame_fuzzer.cc configbin.o value.o frame.o \
    -I"$SRC/src" \
    -o "$OUT/frame_fuzzer"

$CXX $CXXFLAGS $LIB_FUZZING_ENGINE \
    fuzz/overlay_fuzzer.cc configbin.o value.o frame.o buffer.o overlay.o \
    -I"$SRC/src" \
    -o "$OUT/overlay_fuzzer"
