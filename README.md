# libconfigbin

A compact binary configuration-file format and parser library written in C.

## Format

Files use a typed, length-prefixed binary encoding (see `src/configbin.h`).
Supported value types: null, uint32, int64, float64, string, blob, typed array,
section-reference, bool, and timestamp.

A framing layer (`src/frame.h`) wraps multiple config documents into a
byte stream (useful for network transport or append-only config logs).

## Building

```
make          # build static library and tools
make test     # build and run unit tests
make seeds    # (re)generate seed corpus for fuzzers
```

Requires a C11-compatible compiler.  The fuzzing harnesses require clang with
libFuzzer support (available via ClusterFuzzLite).

## Fuzzing

The `fuzz/` directory contains three libFuzzer harnesses:

- **config_fuzzer** — directly fuzzes `bcfg_parse()` and the lookup/access APIs.
- **frame_fuzzer** — fuzzes the framing layer together with the parser.
- **overlay_fuzzer** — fuzzes the streaming overlay accumulator over a framed stream.

Seed corpus lives in `fuzz/corpus/`.  The optional dictionary
`fuzz/dictionary.txt` contains format magic bytes and type tags.

ClusterFuzzLite build configuration is in `.clusterfuzzlite/build.sh`.

## License

MIT
