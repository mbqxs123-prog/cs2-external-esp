#!/usr/bin/env python3
"""Dump the local player's raw bone array (index -> world position).

Bone indices are not in the schema; they belong to the model. When the game
updates, indices drift and the ESP connects wrong joints. This prints every
bone sorted by height so the semantic ones (head / neck / pelvis / feet) can be
re-identified for the current build.
"""
import ctypes
import struct
import sys
from ctypes import wintypes as wt

k32 = ctypes.windll.kernel32
k32.OpenProcess.restype = wt.HANDLE

CLIENT_LOCAL_PAWN = 0x2553D38   # build 14197
OFF_SCENE = 0x330
OFF_MODELSTATE = 0x140
OFF_BONE_ARRAY_IN_MODEL = 0x80
BONE_STRIDE = 0x20
BONE_POS = 0x10


def modules(pid):
    TH32CS_SNAPMODULE = 0x8
    class ME(ctypes.Structure):
        _fields_ = [('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD), ('th32ProcessID', wt.DWORD),
                    ('GlblcntUsage', wt.DWORD), ('ProccntUsage', wt.DWORD),
                    ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', wt.DWORD),
                    ('hModule', wt.HMODULE), ('szModule', wt.WCHAR * 256), ('szExePath', wt.WCHAR * 260)]
    k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)
    me = ME(); me.dwSize = ctypes.sizeof(me)
    out = {}
    ok = k32.Module32FirstW(wt.HANDLE(snap), ctypes.byref(me))
    while ok:
        out[me.szModule.lower()] = me.modBaseAddr
        ok = k32.Module32NextW(wt.HANDLE(snap), ctypes.byref(me))
    k32.CloseHandle(wt.HANDLE(snap))
    return out


def main(pid):
    h = k32.OpenProcess(0x0410, False, pid)
    if not h:
        print("OpenProcess failed", ctypes.get_last_error())
        return 1

    def rd(addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(wt.HANDLE(h), ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
            return None
        return buf.raw

    mods = modules(pid)
    client = mods.get('client.dll')
    print('client.dll base', hex(client))

    pawn = struct.unpack('<Q', rd(client + CLIENT_LOCAL_PAWN, 8))[0]
    print('local pawn', hex(pawn))
    scene = struct.unpack('<Q', rd(pawn + OFF_SCENE, 8))[0]
    arr = struct.unpack('<Q', rd(scene + OFF_MODELSTATE + OFF_BONE_ARRAY_IN_MODEL, 8))[0]
    print('scene', hex(scene), 'bone array', hex(arr))

    rows = []
    for i in range(140):
        raw = rd(arr + i * BONE_STRIDE, 0x1C)
        if not raw:
            break
        x, y, z = struct.unpack_from('<3f', raw, BONE_POS)
        if abs(x) < 1e6 and abs(y) < 1e6 and abs(z) < 1e6 and (x or y or z):
            rows.append((z, i, x, y))

    print('bones read:', len(rows))
    for z, i, x, y in sorted(rows, reverse=True)[:40]:
        print('  idx %3d  z=%7.1f  x=%9.1f y=%9.1f' % (i, z, x, y))
    return 0


if __name__ == '__main__':
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 3008))
