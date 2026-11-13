#pragma once
// FrameData.h -- feeds the driver-written frame (dd0f1a.db) into hzqst/CS2_External.
//
// Data path:  cs2.exe --(MmCopyVirtualMemory in cs2core.sys)--> XOR'd fixed frame
//             on disk --> this file --> CEntity/CView --> Render.hpp / Radar.
// The display side never opens cs2.exe and never calls ReadProcessMemory.
//
// Frame layout (shared/cs2core_abi.h):
//   [0,      10280)   v5  header 168 + 32 x 316 player records
//   [10280,  12604)   v6  AABB[32] + item[64] + checksum
//   [12604,  13968)   v7  grenade/fire records[24] + checksum
// Older producers may write only the v5 part; every tail is validated on its
// own so a missing tail degrades to "no items / no grenades" instead of failing
// the whole frame.

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "Entity.h"
#include "View.hpp"
#include "FrameShm.h"

namespace FrameData
{
    // ---------------------------------------------------------------- layout
    static const uint32_t MAGIC   = 0x45533243u;   // 'C2SE'
    static const uint32_t VERSION = 5u;            // header version (tails carry their own)

    static const int FRAME_V5       = 10280;
    static const int FRAME_V6       = 12604;
    static const int FRAME_V7       = 13968;
    static const int FRAME_SIZE     = FRAME_V7;

    static const int OFF_MAGIC      = 0;
    static const int OFF_VERSION    = 4;
    static const int OFF_SEQ        = 8;
    static const int OFF_FLAGS      = 12;
    static const int OFF_VIEWMAT    = 24;
    static const int OFF_SCREENW    = 88;
    static const int OFF_SCREENH    = 92;
    static const int OFF_LOCALSLOT  = 96;
    static const int OFF_PLAYERCNT  = 100;
    static const int OFF_MAP        = 104;
    static const int OFF_CHECKSUM   = 136;
    static const int OFF_FOV        = 164;
    static const int OFF_PLAYERS    = 168;
    static const int PLAYER_SIZE    = 316;
    static const int MAX_SLOTS      = 32;

    // player record fields
    static const int OFF_P_SLOT     = 0;
    static const int OFF_P_TEAM     = 4;
    static const int OFF_P_HEALTH   = 8;
    static const int OFF_P_LIFESTATE= 12;
    static const int OFF_P_FLAGS    = 16;
    static const int OFF_P_BONEVAL  = 20;
    static const int OFF_P_NAME     = 24;
    static const int OFF_P_ORIGIN   = 56;
    static const int OFF_P_EYEPOS   = 68;
    static const int OFF_P_ANGLES   = 80;
    static const int OFF_P_DIST     = 88;
    static const int OFF_P_BONES    = 92;
    static const int OFF_P_VELOCITY = 296;
    static const int OFF_P_WEAPON   = 308;
    static const int OFF_P_AMMO     = 310;
    static const int OFF_P_ARMOR    = 312;
    static const int OFF_P_GEAR     = 313;

    static const uint32_t PF_ALIVE   = 0x01;
    static const uint32_t PF_LOCAL   = 0x02;
    static const uint32_t PF_DORMANT = 0x04;
    static const uint32_t PF_TEAM    = 0x08;
    static const uint32_t PF_SPOTTED = 0x10;
    static const uint32_t PF_VISIBLE = 0x20;
    static const uint32_t PF_DEFUSING= 0x40;
    static const uint32_t PF_BOMB    = 0x80;

    static const uint8_t GEAR_HELMET   = 0x01;
    static const uint8_t GEAR_DEFUSER  = 0x02;
    static const uint8_t GEAR_ARMORED  = 0x04;
    static const uint8_t GEAR_ZOOMED   = 0x08;
    static const uint8_t GEAR_DEFUSING = 0x10;
    static const uint8_t GEAR_RELOADING= 0x20;

    // v6 tail (items)
    static const int OFF_T_MAGIC    = 10280 + 0;
    static const int OFF_T_VERSION  = 10280 + 4;
    static const int OFF_T_AABBMASK = 10280 + 8;
    static const int OFF_T_ITEMCNT  = 10280 + 12;
    static const int OFF_T_AABB     = 10280 + 16;
    static const int OFF_T_ITEMS    = 10280 + 784;
    static const int ITEM_SIZE      = 24;
    static const int ITEM_MAX       = 64;
    static const int ITEM_TAIL_CHECK= 10280 + 2320;
    static const uint32_t TAIL_MAGIC = 0x4C494154u;  // 'TAIL'

    static const int OFF_I_POS   = 0;
    static const int OFF_I_ID    = 12;
    static const int OFF_I_CAT   = 14;
    static const int OFF_I_FLAGS = 15;
    static const int OFF_I_AMMO  = 16;

    static const uint8_t ITEM_WEAPON  = 0;
    static const uint8_t ITEM_GRENADE = 1;
    static const uint8_t ITEM_C4      = 2;
    static const uint8_t ITEM_KIT     = 3;
    static const uint8_t ITEM_OTHER   = 4;

    static const uint8_t IF_ON_GROUND   = 0x01;
    static const uint8_t IF_HAS_AMMO    = 0x02;
    static const uint8_t IF_PLANTED     = 0x04;
    static const uint8_t IF_DEFUSE_KIT  = 0x08;
    static const uint8_t IF_PROJECTILE  = 0x10;

    // v7 tail (grenades / fire)
    static const int OFF_G_MAGIC    = 12604 + 0;
    static const int OFF_G_VERSION  = 12604 + 4;
    static const int OFF_G_COUNT    = 12604 + 8;
    static const int OFF_G_FLAGS    = 12604 + 12;
    static const int OFF_G_RECS     = 12604 + 16;
    static const int GREN_SIZE      = 56;
    static const int GREN_MAX       = 24;
    static const int GREN_TAIL_CHECK= 12604 + 1360;
    static const uint32_t GRND_MAGIC = 0x444E5247u;  // 'GRND'

    static const int OFF_R_POS      = 0;
    static const int OFF_R_VEL      = 12;
    static const int OFF_R_INITPOS  = 24;
    static const int OFF_R_RADIUS   = 36;
    static const int OFF_R_DETONATE = 40;
    static const int OFF_R_SPAWN    = 44;
    static const int OFF_R_ID       = 48;
    static const int OFF_R_KIND     = 50;
    static const int OFF_R_FLAGS    = 51;
    static const int OFF_R_EXTRA    = 52;

    static const uint8_t GREN_KIND_PROJ = 0;
    static const uint8_t GREN_KIND_FIRE = 1;

    static const uint8_t GREN_F_LIVE    = 0x01;
    static const uint8_t GREN_F_BOUNCED = 0x02;
    static const uint8_t GREN_F_SMOKE   = 0x04;

    static const uint8_t GT_UNKNOWN = 0;
    static const uint8_t GT_HE      = 1;
    static const uint8_t GT_FLASH   = 2;
    static const uint8_t GT_SMOKE   = 3;
    static const uint8_t GT_MOLOTOV = 4;
    static const uint8_t GT_DECOY   = 5;

    // ---------------------------------------------------------------- records
    struct PlayerRec
    {
        int      slot = 0;
        int      team = 0;
        int      health = 0;
        int      lifeState = 0;
        int      armor = 0;
        uint8_t  gear = 0;
        uint32_t flags = 0;
        uint32_t boneMask = 0;
        char     name[33] = { 0 };
        Vec3     origin{};
        Vec3     eye{};
        Vec2     angles{};
        float    distance = 0.f;
        Vec3     velocity{};
        uint16_t weapon = 0;
        uint16_t ammo = 0;
        Vec3     bones[17]{};
    };

    struct ItemRec
    {
        Vec3     pos{};
        uint16_t id = 0;
        uint8_t  cat = ITEM_OTHER;
        uint8_t  flags = 0;
        uint16_t ammo = 0;
    };

    struct GrenRec
    {
        Vec3    pos{};
        Vec3    vel{};
        Vec3    initPos{};
        float   radius = 0.f;
        float   detonate = 0.f;
        float   spawn = 0.f;
        uint16_t id = 0;
        uint8_t kind = GREN_KIND_PROJ;
        uint8_t flags = 0;
        uint32_t extra = 0;
    };

    struct Frame
    {
        uint32_t seq = 0;
        uint32_t flags = 0;
        int      width = 0;
        int      height = 0;
        int      localSlot = -1;
        uint32_t playerCount = 0;
        char     map[33] = { 0 };
        float    fov = 0.f;
        float    view[16]{};
        PlayerRec players[MAX_SLOTS];
        std::vector<ItemRec> items;
        std::vector<GrenRec> grenades;
    };

    // ---------------------------------------------------------------- state
    inline unsigned char g_buf[FRAME_SIZE];
    inline int  g_len = 0;
    inline bool g_ok = false;          // v5 part valid
    inline bool g_tailOk = false;      // v6 part valid
    inline bool g_grenOk = false;      // v7 part valid
    inline int  g_stage = 0;           // 0 ok, 1 open, 2 read, 3 size, 4 magic, 5 version, 6 seq, 7 checksum
    inline Frame g_frame;
    static int g_w = 1920, g_h = 1080;

    inline void Trace(const char*) {}

    // The producer drops the matrix to all-zero when its sanity check fails
    // (scope transitions update dwViewMatrix late). Keep the last good one so a
    // single bad frame does not blank every box.
    inline float g_vm_cache[16] = { 0 };
    inline bool  g_vm_cache_ok = false;

    inline bool ViewMatrixSane(const float* m)
    {
        for (int i = 0; i < 16; i++) { if (!(m[i] == m[i])) return false; }
        const float rl2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
        const float ul2 = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
        const float fl2 = m[12] * m[12] + m[13] * m[13] + m[14] * m[14];
        return rl2 > 0.002f && rl2 < 64.f && ul2 > 0.002f && ul2 < 64.f &&
               fl2 > 0.5f && fl2 < 2.f;
    }
    inline bool Ok() { return g_ok; }
    inline bool TailOk() { return g_tailOk; }
    inline bool GrenOk() { return g_grenOk; }
    inline int  Stage() { return g_stage; }
    inline int  Width() { return g_w; }
    inline int  Height() { return g_h; }
    inline const Frame& Get() { return g_frame; }

    // ---------------------------------------------------------------- readers
    inline uint32_t Rd32(const unsigned char* p) { uint32_t v; memcpy(&v, p, 4); return v; }
    inline int32_t  Ri32(const unsigned char* p) { int32_t v; memcpy(&v, p, 4); return v; }
    inline uint16_t Rd16(const unsigned char* p) { uint16_t v; memcpy(&v, p, 2); return v; }
    inline float    Rf32(const unsigned char* p) { float v; memcpy(&v, p, 4); return v; }
    inline void     RVec3(const unsigned char* p, Vec3& v) { v.x = Rf32(p); v.y = Rf32(p + 4); v.z = Rf32(p + 8); }

    inline uint32_t Fnv1a(const unsigned char* p, int len)
    {
        uint32_t h = 2166136261u;
        for (int i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
        return h;
    }

    // Names for the item definition index. Values not listed fall back to "#id".
    inline const char* WeaponName(uint16_t id)
    {
        switch (id)
        {
        case 1:  return "Desert Eagle";
        case 2:  return "Dual Berettas";
        case 3:  return "Five-SeveN";
        case 4:  return "Glock-18";
        case 7:  return "AK-47";
        case 8:  return "AUG";
        case 9:  return "AWP";
        case 10: return "FAMAS";
        case 11: return "G3SG1";
        case 13: return "Galil AR";
        case 14: return "M249";
        case 16: return "M4A4";
        case 17: return "MAC-10";
        case 19: return "P90";
        case 23: return "MP5-SD";
        case 24: return "UMP-45";
        case 25: return "XM1014";
        case 26: return "PP-Bizon";
        case 27: return "MAG-7";
        case 28: return "Negev";
        case 29: return "Sawed-Off";
        case 30: return "Tec-9";
        case 31: return "Zeus x27";
        case 32: return "P2000";
        case 33: return "MP7";
        case 34: return "MP9";
        case 35: return "Nova";
        case 36: return "P250";
        case 38: return "SCAR-20";
        case 39: return "SG 553";
        case 40: return "SSG 08";
        case 41: return "Knife";
        case 42: return "Flashbang";
        case 43: return "HE Grenade";
        case 44: return "Smoke Grenade";
        case 45: return "Molotov";
        case 46: return "Decoy";
        case 47: return "Incendiary";
        case 48: return "C4";
        case 59: return "Knife";
        case 60: return "M4A1-S";
        case 61: return "USP-S";
        case 63: return "CZ75-Auto";
        case 64: return "R8 Revolver";
        default: return nullptr;
        }
    }

    inline std::string ItemLabel(uint16_t id, uint8_t cat)
    {
        const char* n = WeaponName(id);
        if (n) return std::string(n);
        if (cat == ITEM_C4)   return "C4";
        if (cat == ITEM_KIT)  return "Defuse Kit";
        char b[24]; sprintf_s(b, sizeof(b), "#%u", (unsigned)id);
        return std::string(b);
    }

    inline const char* GrenadeName(uint8_t kind, uint16_t id)
    {
        if (kind == GREN_KIND_FIRE) return "Fire";
        switch (id)
        {
        case 42: return "Flash";
        case 43: return "HE";
        case 44: return "Smoke";
        case 45: return "Molotov";
        case 46: return "Decoy";
        case 47: return "Incendiary";
        default: break;
        }
        return "Grenade";
    }

    inline const char* GrenadeTypeName(uint8_t type)
    {
        switch (type)
        {
        case GT_HE:      return "HE";
        case GT_FLASH:   return "Flash";
        case GT_SMOKE:   return "Smoke";
        case GT_MOLOTOV: return "Molotov";
        case GT_DECOY:   return "Decoy";
        default:         return "Grenade";
        }
    }

    // ---------------------------------------------------------------- reload
    static const char* kFramePath = "C:\\ProgramData\\Microsoft\\Windows\\Caches\\dd0f1a.db";

    // path == nullptr : shared section first, hidden file only as a fallback.
    // path != nullptr : force that file (used by --frame-check <file>).
    inline bool Reload(const char* path = nullptr)
    {
        g_ok = g_tailOk = g_grenOk = false;
        g_frame.items.clear();
        g_frame.grenades.clear();

        bool got = false;
        if (path == nullptr) { got = FrameShm::Read(g_buf, FRAME_SIZE); }

        if (got)
        {
            g_len = FRAME_SIZE;        // the section always holds a full v7 frame
        }
        else
        {
            const char* p = (path != nullptr) ? path : kFramePath;
            HANDLE f = CreateFileA(p, GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f == INVALID_HANDLE_VALUE) { g_stage = 1; return false; }
            DWORD bytes = 0;
            BOOL r = ReadFile(f, g_buf, FRAME_SIZE, &bytes, NULL);
            CloseHandle(f);
            if (!r) { g_stage = 2; return false; }
            if (bytes < (DWORD)FRAME_V5) { g_stage = 3; return false; }
            g_len = (int)bytes;
            // The file mirror is XOR'd; the shared section is not (it never
            // touches a disk, so there is nothing to obfuscate it from).
            for (int i = 0; i < g_len; i++) g_buf[i] ^= 0xA7;
        }

        if (Rd32(g_buf + OFF_MAGIC) != MAGIC) { g_stage = 4; return false; }
        if (Rd32(g_buf + OFF_VERSION) != VERSION) { g_stage = 5; return false; }
        // seqlock: producer sets the low bit while writing. An odd generation
        // means this read raced the writer; drop the frame instead of showing
        // half-old / half-new positions.
        g_frame.seq = Rd32(g_buf + OFF_SEQ);
        if ((g_frame.seq & 1u) != 0u) { g_stage = 6; return false; }
        if (Fnv1a(g_buf + OFF_PLAYERS, FRAME_V5 - OFF_PLAYERS) != Rd32(g_buf + OFF_CHECKSUM))
        { g_stage = 7; return false; }

        // ---- v5 header ----
        g_frame.flags = Rd32(g_buf + OFF_FLAGS);
        g_frame.width = Ri32(g_buf + OFF_SCREENW);
        g_frame.height = Ri32(g_buf + OFF_SCREENH);
        g_frame.localSlot = Ri32(g_buf + OFF_LOCALSLOT);
        g_frame.playerCount = Rd32(g_buf + OFF_PLAYERCNT);
        memcpy(g_frame.map, g_buf + OFF_MAP, 32);
        g_frame.map[32] = 0;
        g_frame.fov = Rf32(g_buf + OFF_FOV);
        memcpy(g_frame.view, g_buf + OFF_VIEWMAT, 64);
        if (ViewMatrixSane(g_frame.view))
        {
            memcpy(g_vm_cache, g_frame.view, 64);
            g_vm_cache_ok = true;
        }
        else if (g_vm_cache_ok)
        {
            memcpy(g_frame.view, g_vm_cache, 64);
        }

        g_w = g_frame.width; if (g_w < 320) g_w = 1920;
        g_h = g_frame.height; if (g_h < 240) g_h = 1080;

        for (int i = 0; i < MAX_SLOTS; i++)
        {
            const unsigned char* p = g_buf + OFF_PLAYERS + i * PLAYER_SIZE;
            PlayerRec& o = g_frame.players[i];
            o = PlayerRec{};
            o.slot = Ri32(p + OFF_P_SLOT);
            o.team = Ri32(p + OFF_P_TEAM);
            o.health = Ri32(p + OFF_P_HEALTH);
            o.lifeState = Ri32(p + OFF_P_LIFESTATE);
            o.flags = Rd32(p + OFF_P_FLAGS);
            o.boneMask = Rd32(p + OFF_P_BONEVAL);
            // name is char[32] and may not be NUL-terminated
            size_t nl = 0; while (nl < 32 && p[OFF_P_NAME + nl]) nl++;
            memcpy(o.name, p + OFF_P_NAME, nl); o.name[nl] = 0;
            RVec3(p + OFF_P_ORIGIN, o.origin);
            RVec3(p + OFF_P_EYEPOS, o.eye);
            o.angles.x = Rf32(p + OFF_P_ANGLES);
            o.angles.y = Rf32(p + OFF_P_ANGLES + 4);
            o.distance = Rf32(p + OFF_P_DIST);
            RVec3(p + OFF_P_VELOCITY, o.velocity);
            o.weapon = Rd16(p + OFF_P_WEAPON);
            o.ammo = Rd16(p + OFF_P_AMMO);
            o.armor = (int)*(const unsigned char*)(p + OFF_P_ARMOR);
            o.gear = *(const unsigned char*)(p + OFF_P_GEAR);
            for (int b = 0; b < 17; b++) RVec3(p + OFF_P_BONES + b * 12, o.bones[b]);
        }
        g_ok = true;
        g_stage = 0;

        // ---- v6 tail: collision boxes + ground items ----
        if (g_len >= FRAME_V6)
        {
            if (Rd32(g_buf + OFF_T_MAGIC) == TAIL_MAGIC && Rd32(g_buf + OFF_T_VERSION) == 1u &&
                Fnv1a(g_buf + 10280, 2320) == Rd32(g_buf + ITEM_TAIL_CHECK))
            {
                g_tailOk = true;
                uint32_t cnt = Rd32(g_buf + OFF_T_ITEMCNT);
                if (cnt > ITEM_MAX) cnt = ITEM_MAX;
                g_frame.items.reserve(cnt);
                for (uint32_t i = 0; i < cnt; i++)
                {
                    const unsigned char* q = g_buf + OFF_T_ITEMS + i * ITEM_SIZE;
                    ItemRec it;
                    RVec3(q + OFF_I_POS, it.pos);
                    it.id = Rd16(q + OFF_I_ID);
                    it.cat = *(q + OFF_I_CAT);
                    it.flags = *(q + OFF_I_FLAGS);
                    it.ammo = Rd16(q + OFF_I_AMMO);
                    if (it.id == 0 && it.cat == ITEM_OTHER) continue;
                    g_frame.items.push_back(it);
                }
            }
        }

        // ---- v7 tail: live grenades + fire areas ----
        if (g_len >= FRAME_V7)
        {
            if (Rd32(g_buf + OFF_G_MAGIC) == GRND_MAGIC && Rd32(g_buf + OFF_G_VERSION) == 1u &&
                Fnv1a(g_buf + 12604, 1360) == Rd32(g_buf + GREN_TAIL_CHECK))
            {
                g_grenOk = true;
                uint32_t cnt = Rd32(g_buf + OFF_G_COUNT);
                if (cnt > GREN_MAX) cnt = GREN_MAX;
                g_frame.grenades.reserve(cnt);
                for (uint32_t i = 0; i < cnt; i++)
                {
                    const unsigned char* q = g_buf + OFF_G_RECS + i * GREN_SIZE;
                    GrenRec g;
                    RVec3(q + OFF_R_POS, g.pos);
                    RVec3(q + OFF_R_VEL, g.vel);
                    RVec3(q + OFF_R_INITPOS, g.initPos);
                    g.radius = Rf32(q + OFF_R_RADIUS);
                    g.detonate = Rf32(q + OFF_R_DETONATE);
                    g.spawn = Rf32(q + OFF_R_SPAWN);
                    g.id = Rd16(q + OFF_R_ID);
                    g.kind = *(q + OFF_R_KIND);
                    g.flags = *(q + OFF_R_FLAGS);
                    g.extra = Rd32(q + OFF_R_EXTRA);
                    g_frame.grenades.push_back(g);
                }
            }
        }
        return true;
    }

    // ---------------------------------------------------------------- helpers
    inline void ApplyView(CView& view)
    {
        if (!g_ok) return;
        const float* m = (const float*)g_buf + (OFF_VIEWMAT / 4);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                view.Matrix[r][c] = m[r * 4 + c];
    }

    inline int LocalSlot()
    {
        if (!g_ok) return -1;
        // Trust the PF_LOCAL flag over the header field: the header's index and
        // the flagged record can disagree by one slot after a map change.
        for (int i = 0; i < MAX_SLOTS; i++)
            if (g_frame.players[i].flags & PF_LOCAL) return i;
        int s = g_frame.localSlot;
        if (s >= 0 && s < MAX_SLOTS) return s;
        return -1;
    }

    inline bool LocalOrigin(Vec3& out, float& yaw)
    {
        int s = LocalSlot();
        if (s < 0) return false;
        const PlayerRec& p = g_frame.players[s];
        out = p.origin;
        yaw = p.angles.y;
        return true;
    }

    inline bool LocalEye(Vec3& out, float& yaw)
    {
        int s = LocalSlot();
        if (s < 0) return false;
        const PlayerRec& p = g_frame.players[s];
        out = p.eye;
        yaw = p.angles.y;
        return true;
    }

    // driver bone slot -> reference project BONEINDEX (24 slots)
    // driver slots 6/9 carry the ELBOW (arm_lower), not the upper arm, so the
    // chain must skip ARM_UPPER_* (which has no data) or it breaks mid-arm.
    static const int SLOT_TO_BONE[17] = {
        HEAD, NECK_0, CHEST, SPINE_1, PELVIS,
        CLAVICLE_L, ARM_LOWER_L, HAND_L,
        CLAVICLE_R, ARM_LOWER_R, HAND_R,
        HIP_L, KNEE_L, ANKLE_L,
        HIP_R, KNEE_R, ANKLE_R
    };

    inline bool FillEntity(int slot, CEntity& e, CView& view)
    {
        const PlayerRec& p = g_frame.players[slot];

        e.Controller.m_iHealth = p.health;
        e.Controller.m_iTeamNum = p.team;
        e.Controller.m_bPawnIsAlive = (p.flags & PF_ALIVE) ? 1 : 0;
        e.Controller.PlayerName = std::string(p.name);

        e.Pawn.m_iHealth = p.health;
        e.Pawn.m_iTeamNum = p.team;
        // Non-zero sentinel: legacy code (Entity.cpp) treats Address == 0 as
        // "no bone data". Nothing dereferences it in the frame-fed build.
        e.Pawn.Address = 1;
        e.Pawn.m_iArmor = p.armor;
        e.Pawn.m_iGear = p.gear;
        e.Pawn.m_iWeaponId = p.weapon;
        e.Pawn.m_iAmmo = p.ammo;
        e.Pawn.m_flDistance = p.distance;
        e.Pawn.m_vVelocity = p.velocity;
        e.Pawn.m_bDormant = (p.flags & PF_DORMANT) != 0;
        e.Pawn.m_bSpotted = (p.flags & PF_SPOTTED) != 0;

        e.Pawn.m_vOldOrigin = p.origin;
        e.Pawn.ViewAngle.x = p.angles.x;
        e.Pawn.ViewAngle.y = p.angles.y;

        const char* wn = WeaponName(p.weapon);
        if (wn) e.Pawn.WeaponName = wn;
        else { char b[32]; sprintf_s(b, sizeof(b), "weapon_%u", (unsigned)p.weapon); e.Pawn.WeaponName = b; }

        Vec2 feet;
        if (!view.WorldToScreen(p.origin, feet)) return false;
        e.Pawn.ScreenPos = feet;

        if (e.Pawn.BoneData.BonePosList.size() < 24) e.Pawn.BoneData.BonePosList.assign(24, BoneJointPos{});
        for (int b = 0; b < 17; b++)
        {
            const Vec3& wp = p.bones[b];
            // A zero transform means the driver had no data for this slot.
            // Filling it anyway makes DrawBone connect the skeleton to the
            // world origin, which shows up as long lines across the screen.
            if (wp.x == 0.f && wp.y == 0.f && wp.z == 0.f) continue;

            int idx = SLOT_TO_BONE[b];
            BoneJointPos& bp = e.Pawn.BoneData.BonePosList[idx];
            bp.Pos = wp;
            Vec2 sp;
            bp.IsVisible = view.WorldToScreen(bp.Pos, sp);
            if (bp.IsVisible)
            {
                // Projected far outside the frame: treat as invalid so one bad
                // joint cannot draw a line across the whole screen.
                if (sp.x < -(float)g_w || sp.x > (float)g_w * 2.f ||
                    sp.y < -(float)g_h || sp.y > (float)g_h * 2.f)
                {
                    bp.IsVisible = false;
                }
            }
            bp.ScreenPos = sp;
        }
        return true;
    }

    inline int BuildEntities(std::vector<CEntity>& out, CView& view, bool includeTeam = false)
    {
        out.clear();
        if (!g_ok) return 0;
        // emplace in place: CEntity owns a vector/string, push_back of a temporary
        // invalidates BonePosList pointers used later by the renderer.
        out.reserve(MAX_SLOTS);
        int n = 0;
        for (int i = 0; i < MAX_SLOTS; i++)
        {
            const PlayerRec& p = g_frame.players[i];
            if (p.flags & PF_LOCAL) continue;
            if (!(p.flags & PF_ALIVE)) continue;
            if (p.health <= 0) continue;
            if (p.flags & PF_DORMANT) continue;
            if ((p.flags & PF_TEAM) && !includeTeam) continue;

            out.emplace_back();
            CEntity& e = out.back();
            if (!FillEntity(i, e, view)) { out.pop_back(); continue; }
            n++;
        }
        return n;
    }
}
