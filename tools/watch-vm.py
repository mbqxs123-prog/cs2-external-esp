#!/usr/bin/env python3
"""Watch the live frame section and log view-matrix / FOV changes.

Used to see what CS2 does to dwViewMatrix while scoping: a scope switch
changes the projection FOV, so row norms and the projected position of a fixed
player should move in a predictable way. If the matrix instead goes to zero or
fails the sanity check, boxes disappear.
"""
import ctypes
import struct
import sys
import time
from ctypes import wintypes as wt

k32 = ctypes.windll.kernel32

SALTS = (3, 2, 1)
V5 = 10280
PLAYER = 316
OFF_PLAYERS = 168


def build_name(pid, salt):
    a = (0x4C325343 ^ pid) ^ (salt << 24)
    b = (0x9E3779B9 * (pid + 1)) & 0xFFFFFFFF
    b >>= 16
    c = ((pid << 16) ^ 0x5F3A) & 0xFFFF
    d = ((pid >> 3) ^ 0xA1B2C3D4) & 0xFFFF
    e = (0x7F4A7C15 + pid) & 0xFFFFFFFF
    return "Global\\{%08X-%04X-%04X-%04X-%08X%04X}" % (a, b & 0xFFFF, c, d, e, pid & 0xFFFF)


def open_view(pid):
    k32.OpenFileMappingW.restype = wt.HANDLE
    k32.MapViewOfFile.restype = ctypes.c_void_p
    for salt in SALTS:
        h = k32.OpenFileMappingW(4, False, build_name(pid, salt))
        if h:
            p = k32.MapViewOfFile(wt.HANDLE(h), 4, 0, 0, 0)
            if p:
                return p
    return None


def main(pid, seconds):
    view = open_view(pid)
    if not view:
        print("no section for pid", pid)
        return 1
    print("pid", pid, "view", hex(view))
    t0 = time.time()
    last = None
    while time.time() - t0 < seconds:
        raw = ctypes.string_at(view, V5)
        seq = struct.unpack_from('<I', raw, 8)[0]
        if seq & 1:
            time.sleep(0.02)
            continue
        m = struct.unpack_from('<16f', raw, 24)
        fov = struct.unpack_from('<f', raw, 164)[0]
        rl2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2]
        fl2 = m[12] * m[12] + m[13] * m[13] + m[14] * m[14]
        # first non-local alive player with data
        sx = sy = None
        for i in range(32):
            off = OFF_PLAYERS + i * PLAYER
            flags = struct.unpack_from('<I', raw, off + 16)[0]
            hp = struct.unpack_from('<i', raw, off + 8)[0]
            if flags & 2 or not (flags & 1) or hp <= 0:
                continue
            ox, oy, oz = struct.unpack_from('<3f', raw, off + 56)
            w = m[12] * ox + m[13] * oy + m[14] * oz + m[15]
            if w > 0.01:
                sx = (1920 * 0.5) + (m[0] * ox + m[1] * oy + m[2] * oz + m[3]) / w * (1920 * 0.5)
                sy = (1080 * 0.5) - (m[4] * ox + m[5] * oy + m[6] * oz + m[7]) / w * (1080 * 0.5)
            break
        cur = (round(rl2, 3), round(fl2, 3), round(fov, 1))
        if last is None or abs(cur[0] - last[0]) > 0.002 or abs(cur[2] - last[2]) > 0.5:
            print("t=%.2f rl2=%.4f fl2=%.4f fov=%.1f m0=%.4f m15=%.2f p=(%s,%s)"
                  % (time.time() - t0, rl2, fl2, fov, m[0], m[15],
                     "%.0f" % sx if sx is not None else "-",
                     "%.0f" % sy if sy is not None else "-"))
            last = cur
        time.sleep(0.05)
    return 0


if __name__ == '__main__':
    pid = int(sys.argv[1]) if len(sys.argv) > 1 else 3008
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 40
    sys.exit(main(pid, secs))
