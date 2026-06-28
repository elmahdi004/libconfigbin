#!/usr/bin/env python3
"""
Generate seed corpus for libconfigbin fuzzers.

Run: python3 gen_seeds.py

Writes binary seed files into:
  fuzz/corpus/config_fuzzer/
  fuzz/corpus/frame_fuzzer/
"""

import struct
import os

MAGIC   = b'BCFG'
VERSION = 1

def hdr(num_sections: int) -> bytes:
    return MAGIC + struct.pack('<HH', VERSION, num_sections)

def section(name: bytes, entries: bytes) -> bytes:
    assert len(name) < 256
    return struct.pack('B', len(name)) + name + struct.pack('<I', 0) + entries

def section_with_n(name: bytes, n: int, entries: bytes) -> bytes:
    assert len(name) < 256
    return struct.pack('B', len(name)) + name + struct.pack('<I', n) + entries

def entry(key: bytes, value_bytes: bytes) -> bytes:
    assert len(key) < 256
    return struct.pack('B', len(key)) + key + value_bytes

def val_null() -> bytes:
    return b'\x00'

def val_uint32(v: int) -> bytes:
    return b'\x01' + struct.pack('<I', v)

def val_int64(v: int) -> bytes:
    return b'\x02' + struct.pack('<q', v)

def val_float64(v: float) -> bytes:
    return b'\x03' + struct.pack('<d', v)

def val_string(s: bytes) -> bytes:
    return b'\x04' + struct.pack('<H', len(s)) + s

def val_blob(b: bytes) -> bytes:
    return b'\x05' + struct.pack('<I', len(b)) + b

def val_array(elem_type: int, elems: list, fmt: str) -> bytes:
    data = b''.join(struct.pack(fmt, e) for e in elems)
    return b'\x06' + struct.pack('B', elem_type) + struct.pack('<H', len(elems)) + data

def val_bool(v: bool) -> bytes:
    return b'\x08' + (b'\x01' if v else b'\x00')

def val_timestamp(ns: int) -> bytes:
    return b'\x09' + struct.pack('<q', ns)

def frame(doc: bytes, flags: int = 0x04, pad_len: int = 0) -> bytes:
    sync = struct.pack('<H', 0xBEBC)
    hd = sync + struct.pack('BB', flags, pad_len) + struct.pack('<I', len(doc))
    return hd + doc + (b'\x00' * pad_len)


def write_seed(path: str, data: bytes):
    with open(path, 'wb') as f:
        f.write(data)
    print(f'  wrote {path} ({len(data)} bytes)')


def build_config_seeds():
    d = 'fuzz/corpus/config_fuzzer'
    os.makedirs(d, exist_ok=True)

    # seed1: empty doc
    write_seed(f'{d}/seed_empty', hdr(0))

    # seed2: one section, one uint32 entry
    e1 = entry(b'port', val_uint32(8080))
    s1 = section_with_n(b'network', 1, e1)
    write_seed(f'{d}/seed_one_section', hdr(1) + s1)

    # seed3: string + bool + null
    e_str  = entry(b'host', val_string(b'localhost'))
    e_bool = entry(b'debug', val_bool(True))
    e_null = entry(b'reserved', val_null())
    s2 = section_with_n(b'app', 3, e_str + e_bool + e_null)
    write_seed(f'{d}/seed_string_bool', hdr(1) + s2)

    # seed4: int64 + float64 + timestamp
    e_i   = entry(b'offset', val_int64(-1000))
    e_f   = entry(b'ratio', val_float64(3.14159))
    e_ts  = entry(b'created', val_timestamp(1_700_000_000_000_000_000))
    s3 = section_with_n(b'metrics', 3, e_i + e_f + e_ts)
    write_seed(f'{d}/seed_numeric_types', hdr(1) + s3)

    # seed5: array of uint32
    e_arr = entry(b'ids', val_array(0x01, [1, 2, 3, 42, 100], '<I'))
    s4 = section_with_n(b'data', 1, e_arr)
    write_seed(f'{d}/seed_array_uint32', hdr(1) + s4)

    # seed6: array of int64
    e_arr2 = entry(b'vals', val_array(0x02, [-1, 0, 1, 9999999], '<q'))
    s5 = section_with_n(b'data2', 1, e_arr2)
    write_seed(f'{d}/seed_array_int64', hdr(1) + s5)

    # seed7: array of float64
    e_arr3 = entry(b'pts', val_array(0x03, [0.0, 1.0, -1.0, 3.14], '<d'))
    s6 = section_with_n(b'floats', 1, e_arr3)
    write_seed(f'{d}/seed_array_float64', hdr(1) + s6)

    # seed8: secref
    e_ref = entry(b'cfg', b'\x07' + struct.pack('<H', 1))
    s_main = section_with_n(b'main', 1, e_ref)
    s_sub  = section_with_n(b'sub', 0, b'')
    write_seed(f'{d}/seed_secref', hdr(2) + s_main + s_sub)

    # seed9: blob
    e_blob = entry(b'data', val_blob(b'\x00\x01\x02\x03' * 16))
    s7 = section_with_n(b'raw', 1, e_blob)
    write_seed(f'{d}/seed_blob', hdr(1) + s7)

    # seed10: multiple sections, mixed types
    entries_a = (entry(b'x', val_uint32(1)) +
                 entry(b'y', val_uint32(2)) +
                 entry(b'label', val_string(b'origin')))
    entries_b = (entry(b'enabled', val_bool(True)) +
                 entry(b'timeout', val_int64(5000)))
    sa = section_with_n(b'point', 3, entries_a)
    sb = section_with_n(b'opts', 2, entries_b)
    write_seed(f'{d}/seed_two_sections', hdr(2) + sa + sb)

    # seed11: larger array of bool (elem_type 0x08, 1 byte each)
    bools = [1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1]
    e_barr = entry(b'flags', val_array(0x08, bools, 'B'))
    s8 = section_with_n(b'bitmask', 1, e_barr)
    write_seed(f'{d}/seed_array_bool', hdr(1) + s8)

    # seed12: array of timestamp (elem_type 0x09, 8 bytes each)
    ts_vals = [1_600_000_000_000_000_000,
               1_700_000_000_000_000_000,
               1_800_000_000_000_000_000]
    e_tsarr = entry(b'events', val_array(0x09, ts_vals, '<q'))
    s9 = section_with_n(b'timeline', 1, e_tsarr)
    write_seed(f'{d}/seed_array_timestamp', hdr(1) + s9)


def build_frame_seeds():
    d = 'fuzz/corpus/frame_fuzzer'
    os.makedirs(d, exist_ok=True)

    # build the same docs but wrapped in frames
    doc0 = hdr(0)
    write_seed(f'{d}/seed_empty_frame', frame(doc0))

    e_p = entry(b'port', val_uint32(443))
    s_n = section_with_n(b'net', 1, e_p)
    doc1 = hdr(1) + s_n
    write_seed(f'{d}/seed_one_section_frame', frame(doc1))

    # two-frame stream (first not LAST, second LAST)
    f1 = frame(doc0, flags=0x00)  # not last
    f2 = frame(doc1, flags=0x04)  # last
    write_seed(f'{d}/seed_two_frames', f1 + f2)

    # frame with padding
    write_seed(f'{d}/seed_padded_frame',
               frame(doc0, flags=0x04, pad_len=4))

    # frame with array of int64 (hits the interesting path)
    e_arr = entry(b'vals', val_array(0x02, list(range(10)), '<q'))
    s_d   = section_with_n(b'data', 1, e_arr)
    doc2  = hdr(1) + s_d
    write_seed(f'{d}/seed_frame_array_i64', frame(doc2))


if __name__ == '__main__':
    build_config_seeds()
    build_frame_seeds()
    print('Done.')
