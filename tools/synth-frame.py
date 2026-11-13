#!/usr/bin/env python3
"""Build a synthetic driver frame (ABI v5+v6+v7) for offline parser checks.

Layout mirrors shared/cs2core_abi.h:
  [0,10280)   v5 header + 32 x 316 player records
  [10280,)    v6 tail: AABB[32] + item[64] + checksum
  [12604,)    v7 tail: grenade/fire[24] + checksum
The whole file is XOR'd with 0xA7, exactly like cs2_file.c writes it.
"""
import struct
import sys

XOR = 0xA7
V5, V6, V7 = 10280, 12604, 13968
PLAYER_SIZE = 316

def fnv1a(buf):
    h = 2166136261
    for b in buf:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h

def main(path):
    fr = bytearray(V7)

    # ---- v5 header ----
    struct.pack_into('<IIII', fr, 0, 0x45533243, 5, 2, 0x1)      # magic, ver, seq, flags
    # world -> clip. camera at origin looking down +x:
    #   clip.x = y, clip.y = z, w = x   (see CView::WorldToScreen)
    m = [0.0] * 16
    m[0], m[1], m[2], m[3] = 0.0, 1.0, 0.0, 0.0
    m[4], m[5], m[6], m[7] = 0.0, 0.0, 1.0, 0.0
    m[12], m[13], m[14], m[15] = 1.0, 0.0, 0.0, 0.0
    struct.pack_into('<16f', fr, 24, *m)
    struct.pack_into('<ii', fr, 88, 1920, 1080)                 # screen w/h
    struct.pack_into('<i', fr, 96, 0)                           # local slot
    struct.pack_into('<I', fr, 100, 4)                          # player count
    fr[104:104 + 9] = b'de_dust2\x00'
    struct.pack_into('<f', fr, 164, 90.0)                       # fov

    PF_ALIVE, PF_LOCAL, PF_DORMANT, PF_TEAM, PF_SPOTTED = 1, 2, 4, 8, 0x10
    PF_VISIBLE, PF_DEFUSING, PF_BOMB = 0x20, 0x40, 0x80
    G_HELMET, G_KIT, G_ARMORED, G_ZOOMED, G_DEFUSING, G_RELOAD = 1, 2, 4, 8, 0x10, 0x20

    players = [
        # slot, team, hp, armor, gear, flags, weapon, ammo, dist, org, name
        (0, 2, 100, 100, G_ARMORED | G_HELMET, PF_ALIVE | PF_LOCAL, 7, 30, 0.0, (100, 0, 0), "LocalPlayer"),
        (1, 2, 77, 100, G_ARMORED | G_HELMET | G_ZOOMED, PF_ALIVE | PF_SPOTTED | PF_VISIBLE, 9, 5, 12.3, (120, -40, 0), "EnemyAWPer"),
        (2, 2, 32, 0, G_RELOAD, PF_ALIVE, 16, 25, 25.0, (140, 30, 0), "EnemyRifler"),
        (3, 3, 100, 50, G_ARMORED | G_KIT, PF_ALIVE | PF_TEAM, 60, 20, 8.0, (110, 45, 0), "TeammateCT"),
        (4, 2, 55, 0, 0, PF_ALIVE | PF_BOMB, 7, 12, 30.0, (160, -10, 0), "Bomber"),
    ]

    for slot, team, hp, armor, gear, flags, wpn, ammo, dist, org, name in players:
        p = 168 + slot * PLAYER_SIZE
        struct.pack_into('<i', fr, p + 0, slot)
        struct.pack_into('<i', fr, p + 4, team)
        struct.pack_into('<i', fr, p + 8, hp)
        struct.pack_into('<i', fr, p + 12, 0)                   # life state alive
        struct.pack_into('<I', fr, p + 16, flags)
        struct.pack_into('<I', fr, p + 20, 0x1FFFF)             # all 17 bones valid
        nb = name.encode()
        fr[p + 24:p + 24 + len(nb)] = nb
        struct.pack_into('<3f', fr, p + 56, *org)               # origin (feet)
        struct.pack_into('<3f', fr, p + 68, org[0], org[1], org[2] + 64.0)  # eye
        struct.pack_into('<2f', fr, p + 80, 0.0, 0.0)           # pitch, yaw
        struct.pack_into('<f', fr, p + 88, dist)
        # 17 bones, 72 units tall, head on top
        heights = [72, 66, 58, 50, 44, 58, 52, 44, 58, 52, 44, 40, 22, 2, 40, 22, 2]
        for i, z in enumerate(heights):
            struct.pack_into('<3f', fr, p + 92 + i * 12, org[0], org[1], org[2] + z)
        struct.pack_into('<3f', fr, p + 296, 0.0, 0.0, 0.0)     # velocity
        struct.pack_into('<H', fr, p + 308, wpn)
        struct.pack_into('<H', fr, p + 310, ammo)
        fr[p + 312] = armor
        fr[p + 313] = gear

    v5_check = fnv1a(fr[168:V5])
    struct.pack_into('<I', fr, 136, v5_check)

    # ---- v6 tail: items ----
    struct.pack_into('<IIII', fr, V5, 0x4C494154, 1, 0, 3)      # 'TAIL', ver 1, aabb mask, item count
    I_ON_GROUND, I_HAS_AMMO, I_PLANTED = 0x01, 0x02, 0x04
    items = [
        # pos, id, cat, flags, ammo
        ((130, -20, 0), 7, 0, I_ON_GROUND | I_HAS_AMMO, 30),
        ((150, 10, 0), 48, 2, I_PLANTED, 0),
        ((145, 20, 0), 44, 1, I_ON_GROUND, 1),
    ]
    for i, (pos, iid, cat, flags, ammo) in enumerate(items):
        q = V5 + 784 + i * 24
        struct.pack_into('<3f', fr, q, *pos)
        struct.pack_into('<H', fr, q + 12, iid)
        fr[q + 14] = cat
        fr[q + 15] = flags
        struct.pack_into('<H', fr, q + 16, ammo)
    struct.pack_into('<I', fr, V5 + 2320, fnv1a(fr[V5:V5 + 2320]))

    # ---- v7 tail: grenades / fire ----
    struct.pack_into('<IIII', fr, V6, 0x444E5247, 1, 2, 1)      # 'GRND', ver 1, count 2, flags live
    grens = [
        # pos, vel, initpos, radius, detonate, spawn, id, kind, flags, extra
        ((135, 5, 40), (300, 0, -50), (120, -5, 60), 350.0, 30.0, 28.0, 43, 0, 0x02, 1),
        ((155, -30, 0), (0, 0, 0), (150, -30, 0), 180.0, 40.0, 32.0, 45, 1, 0x01, 37),
    ]
    for i, (pos, vel, ip, radius, det, spawn, gid, kind, flags, extra) in enumerate(grens):
        q = V6 + 16 + i * 56
        struct.pack_into('<3f', fr, q, *pos)
        struct.pack_into('<3f', fr, q + 12, *vel)
        struct.pack_into('<3f', fr, q + 24, *ip)
        struct.pack_into('<f', fr, q + 36, radius)
        struct.pack_into('<f', fr, q + 40, det)
        struct.pack_into('<f', fr, q + 44, spawn)
        struct.pack_into('<H', fr, q + 48, gid)
        fr[q + 50] = kind
        fr[q + 51] = flags
        struct.pack_into('<I', fr, q + 52, extra)
    struct.pack_into('<I', fr, V6 + 1360, fnv1a(fr[V6:V6 + 1360]))

    # ---- encrypt like cs2_file.c ----
    out = bytes(b ^ XOR for b in fr)
    with open(path, 'wb') as f:
        f.write(out)
    print("wrote %s (%d bytes) v5checksum=0x%08X" % (path, len(out), v5_check))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'docs/synth-frame.db')
