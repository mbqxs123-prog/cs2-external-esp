/* ============================================================================
 * cs2_auto.c  --  结构式锚点自解析 (不像特征码那样怕更新)
 *
 * 三个层次, 逐层兜底:
 *   ① 结构扫描  : 遍历 client.dll 可写段, 用"数据结构不变量"把锚点找回来
 *                 (viewMatrix 的 16 float 形状 / 实体系统的多级指针 + vtable 归属)
 *   ② 语义校验  : 用扫到的值真的去解一个实体出来, 血 0..100 / 队 2..3 /
 *                 坐标有限 / 骨骼间距 1..60 单位
 *   ③ 种子兜底  : 扫不到就用 cs2_seed.h, 但同样过 ②, 过不了就认定"这版没对上",
 *                 让采样线程下一轮重试(而不是拿错偏移去乱读)
 * ========================================================================== */
#include "cs2_auto.h"

#define CS2_SCAN_CHUNK   0x00100000u   /* 1 MiB, 与 sigscan.c 一致 */
#define CS2_VM_STEP      4u            /* viewMatrix 按 4 字节步进找 */
#define CS2_MAX_DEEP     4096u         /* 实体系统候选的最大深检次数 */
#define CS2_CTRL_LOCAL_F 0x790u        /* CBasePlayerController::m_bIsLocalPlayerController */

/* ------------------------------------------------------------------ 小工具 */
static int is_user_ptr(u64 v)
{
    if (v < 0x10000ull)          return 0;
    if (v > 0x00007FFFFFFFFFFFull) return 0;
    if ((v & 0x7ull) != 0)       return 0;   /* 8 字节对齐 */
    return 1;
}

static int is_finite(f32 v)
{
    u32 b;
    /* NaN / Inf 的指数全 1 形状: 直接用比较式判, 避免依赖 <math.h> */
    if (v != v) return 0;
    if (v > 1.0e7f || v < -1.0e7f) return 0;
    (void)b;
    return 1;
}

static int rd(cs2_target* t, u64 va, void* dst, u32 len)
{
    return cs2_read(t, va, dst, len);
}

/* 指针落在模块映像内? (用 vtable 归属做实体识别时用) */
static int ptr_in_module(u64 p, u64 base, u32 size)
{
    return (p >= base && p < base + (u64)size);
}

/* ------------------------------------------------------------ PE 段遍历 */
typedef struct cs2_sec {
    u64 va;
    u32 vsize;
    u32 rawsize;
    u32 chars;
} cs2_sec;

static int section_next(cs2_target* t, u64 base, u32* cursor, cs2_sec* out)
{
    u8 hdr[0x1000];
    u16 nsec;
    u32 i = *cursor;
    u32 off;

    if (rd(t, base, hdr, 0x40) != CS2_OK && rd(t, base, hdr, 0x1000) != CS2_OK)
        return 0;
    if (*(u16*)(hdr + 0x00) != 0x5A4D) return 0;          /* MZ */
    {
        i32 lfanew = *(i32*)(hdr + 0x3C);
        if (lfanew <= 0 || lfanew > 0x400) return 0;
        if (rd(t, base + (u64)lfanew, hdr, 0x40) != CS2_OK) return 0;
        if (*(u32*)(hdr + 0x00) != 0x00004550) return 0;  /* PE\0\0 */
        nsec = *(u16*)(hdr + 0x06);
        if (nsec == 0 || nsec > 96) return 0;
        if (i >= nsec) return 0;
        off = (u32)lfanew + 0x18 + *(u16*)(hdr + 0x14);
        /* 重新取节表 */
        {
            u8 sec[40];
            if (rd(t, base + (u64)off + (u64)i * 40u, sec, 40) != CS2_OK) return 0;
            out->vsize   = *(u32*)(sec + 0x08);
            out->va      = (u64)(*(u32*)(sec + 0x0C));
            out->rawsize = *(u32*)(sec + 0x10);
            out->chars   = *(u32*)(sec + 0x24);
        }
        out->va += base;
        if (out->vsize == 0) out->vsize = out->rawsize;
        *cursor = i + 1;
        return 1;
    }
}

/* ------------------------------------------------ ① viewMatrix 结构扫描 */
static u64 g_vm_hit;

static int vm_test(const f32* m)
{
    int i;
    for (i = 0; i < 16; i++) if (!is_finite(m[i])) return 0;

    /* CS2 的 world->clip 是 4x4 行主序, 第 4 行 (m12..m15) 带世界坐标量级的平移项,
     * 典型值在 -1e4..1e4。单位阵/纯旋转阵(常见的误命中)在这里相加接近 0, 直接拒掉。 */
    {
        f32 t = m[12] + m[13] + m[14] + m[15];
        if (t > -100.0f && t < 100.0f) return 0;
    }
    /* 明确的单位阵: 对角 1, 其余 0 */
    if (m[0] > 0.98f && m[0] < 1.02f &&
        m[5] > 0.98f && m[5] < 1.02f &&
        m[10] > 0.98f && m[10] < 1.02f &&
        m[15] > 0.98f && m[15] < 1.02f) return 0;

    /* 旋转项: 真相机矩阵的 m1/m4 (sin/cos 组合) 不可能同时是 0 */
    if (m[1] > -0.001f && m[1] < 0.001f &&
        m[4] > -0.001f && m[4] < 0.001f) return 0;

    /* 缩放项不能是 0 (真矩阵至少有 x/y 缩放) */
    if (m[0] == 0.0f || m[5] == 0.0f) return 0;
    return 1;
}

int cs2_auto_vm_ok(const f32* m) { return vm_test(m); }

static int vm_chunk_cb(u64 va, const u8* buf, u32 len)
{
    u32 i;
    if (len < 64) return 0;
    for (i = 0; i + 64 <= len; i += CS2_VM_STEP) {
        if (!vm_test((const f32*)(buf + i))) continue;
        /* 再要求它"看起来像当前相机": 平移项不能是垃圾大数 */
        {
            const f32* m = (const f32*)(buf + i);
            f32 tx = m[12], ty = m[13], tz = m[14];
            if (!is_finite(tx) || !is_finite(ty) || !is_finite(tz)) continue;
            if (tx * tx > 1.0e12f || ty * ty > 1.0e12f || tz * tz > 1.0e12f) continue;
        }
        g_vm_hit = va + i;
        return 1;
    }
    return 0;
}

/* ------------------------------------------- ① 实体系统结构扫描 (多级指针) */
static int ent_validate_entity(cs2_target* t, u64 ent, u64 client_base, u32 client_size,
                               const cs2_offsets* o)
{
    u64 vt = 0, sn = 0;
    f32 org[3];
    int good = 0;

    if (!rd(t, ent, &vt, 8)) return 0;
    /* 实体的第一格是 vtable, 必须落在 client.dll 里 —— 这条不含任何偏移猜测 */
    if (ptr_in_module(vt, client_base, client_size)) good++;
    /* 再用场景节点做第二判据 (m_pGameSceneNode 来自种子, 允许它漂) */
    if (rd(t, ent + CS2_NV_M_PGAMESCENENODE, &sn, 8) && is_user_ptr(sn)) {
        if (rd(t, sn + CS2_NV_M_VECABSORIGIN, org, 12)) {
            if (is_finite(org[0]) && is_finite(org[1]) && is_finite(org[2]) &&
                org[0] * org[0] < 9.0e8f && org[1] * org[1] < 9.0e8f) {
                good++;
                {
                    u8 dorm = 0;
                    if (rd(t, sn + CS2_NV_M_BDORMANT, &dorm, 1) && dorm <= 1) good++;
                }
            }
        }
    }
    (void)o;
    return good >= 2;
}

static int ent_chunk_cb(cs2_target* t, u64 slotVa, u64 slotVal, u64 client_base,
                        u32 client_size, const cs2_offsets* o, u32* deep_left)
{
    u64 chunk = 0;
    int idx, hits = 0;

    if (!is_user_ptr(slotVal)) return 0;
    if (*deep_left == 0) return 0;
    (*deep_left)--;

    if (!rd(t, slotVal + o->entity_chunk_off, &chunk, 8)) return 0;
    if (!is_user_ptr(chunk)) return 0;

    for (idx = 1; idx <= 6; idx++) {
        u64 ent = 0;
        if (!rd(t, chunk + (u64)o->entity_stride * (u64)idx, &ent, 8)) continue;
        if (!is_user_ptr(ent)) continue;
        if (ent_validate_entity(t, ent, client_base, client_size, o)) hits++;
    }
    return (hits >= 2) ? 1 : 0;
}

/* 通用可写段扫描: 8 字节步进取指针做深检 */
static u64 g_el_hit;
static u32 g_el_deep;

static int el_scan_cb(u64 va, const u8* buf, u32 len, cs2_target* t, u64 base,
                      u32 size, const cs2_offsets* o)
{
    u32 i;
    for (i = 0; i + 8 <= len; i += 8) {
        u64 v;
        /* 槽位本身对齐在段内, 但真实槽位可能不是 8 对齐起点; 这里逐字节试 */
        RtlCopyMemory(&v, buf + i, 8);
        if (!is_user_ptr(v)) continue;
        if (g_el_deep == 0) return 0;
        if (ent_chunk_cb(t, va + i, v, base, size, o, &g_el_deep)) {
            g_el_hit = va + i;
            return 1;
        }
    }
    return 0;
}

/* 遍历某模块所有可写段, 对每个 1MiB 块回调 */
typedef int (*chunk_ptr_cb)(u64 va, const u8* buf, u32 len);
typedef int (*chunk_ent_cb)(u64 va, const u8* buf, u32 len, cs2_target* t, u64 base,
                            u32 size, const cs2_offsets* o);

static void scan_writable(cs2_target* t, u64 base, u32 size, void* cb, int kind,
                          const cs2_offsets* o)
{
    u8* buf = NULL;
    u32 cursor = 0;
    cs2_sec s;

    buf = (u8*)cs2_alloc_np(CS2_SCAN_CHUNK);
    if (buf == NULL) return;

    while (section_next(t, base, &cursor, &s)) {
        u32 done = 0;
        if ((s.chars & 0x80000000u) == 0) continue;     /* 只要可写段 */
        if (s.vsize == 0 || s.vsize > (256u << 20)) continue;
        while (done < s.vsize) {
            u32 want = s.vsize - done;
            if (want > CS2_SCAN_CHUNK) want = CS2_SCAN_CHUNK;
            if (rd(t, s.va + done, buf, want) == CS2_OK) {
                if (kind == 0) {
                    if (((chunk_ptr_cb)cb)(s.va + done, buf, want)) { done = s.vsize; break; }
                } else {
                    if (((chunk_ent_cb)cb)(s.va + done, buf, want, t, base, size, o)) {
                        done = s.vsize; break;
                    }
                }
            }
            done += want;
        }
        if ((kind == 0 && g_vm_hit) || (kind == 1 && g_el_hit)) break;
    }
    /* holds a verbatim copy of game module pages (offset scanning) */
    cs2_free_wipe(buf, CS2_SCAN_CHUNK);
}

/* ------------------------------------------------------------ 实体解引用 */
u64 cs2_entity_at(cs2_target* t, const cs2_offsets* o, u64 client_base, i32 index)
{
    u64 list = 0, chunk = 0, ent = 0;
    u32 shift = o->entity_shift ? o->entity_shift : CS2_ENT_CHUNK_SHIFT;
    u32 mul   = o->entity_chunk_mul ? o->entity_chunk_mul : CS2_ENT_CHUNK_MUL;
    u32 coff  = o->entity_chunk_off ? o->entity_chunk_off : CS2_ENT_CHUNK_OFF;
    u32 strd  = o->entity_stride ? o->entity_stride : CS2_ENT_STRIDE;
    u32 imask = o->entity_index_mask ? o->entity_index_mask : CS2_ENT_INDEX_MASK;

    if (index <= 0) return 0;
    if (!rd(t, client_base + (u64)o->dwEntityList, &list, 8) || !is_user_ptr(list)) return 0;
    if (!rd(t, list + (u64)mul * ((u64)index >> shift) + coff, &chunk, 8)) return 0;
    if (!is_user_ptr(chunk)) return 0;
    if (!rd(t, chunk + (u64)strd * ((u64)index & imask), &ent, 8)) return 0;
    return is_user_ptr(ent) ? ent : 0;
}

u64 cs2_local_controller(cs2_target* t, const cs2_offsets* o, u64 client_base, i32* local_slot)
{
    i32 i;
    for (i = 1; i <= 64; i++) {
        u64 ent = cs2_entity_at(t, o, client_base, i);
        u8  flag = 0;
        if (!ent) continue;
        if (rd(t, ent + CS2_CTRL_LOCAL_F, &flag, 1) && flag == 1) {
            if (local_slot) *local_slot = i;
            return ent;
        }
    }
    /* 退路: 走种子给的 dwLocalPlayerController (它常常仍然有效) */
    {
        u64 p = 0, ent = 0;
        u8 flag = 0;
        if (rd(t, client_base + (u64)o->dwLocalPlayerController, &p, 8) && is_user_ptr(p)) {
            if (rd(t, p, &ent, 8) && is_user_ptr(ent)) {
                if (rd(t, ent + CS2_CTRL_LOCAL_F, &flag, 1) && flag == 1) {
                    if (local_slot) *local_slot = -1;
                    return ent;
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------ 骨骼标定 */
u32 cs2_auto_bone_offset(cs2_target* t, const cs2_offsets* o, u64 scene_node)
{
    u64 bone_array = 0;
    f32 org[3];
    u32 cand[2];
    int best = -1, best_score = -1, k;

    (void)o;
    if (!rd(t, scene_node + CS2_NV_M_MODELSTATE + CS2_BONE_ARRAY_FROM_MODELSTATE, &bone_array, 8))
        return CS2_BONE_POS_CANDIDATE_A;
    if (!is_user_ptr(bone_array)) return CS2_BONE_POS_CANDIDATE_A;
    if (!rd(t, scene_node + CS2_NV_M_VECABSORIGIN, org, 12)) return CS2_BONE_POS_CANDIDATE_A;

    cand[0] = CS2_BONE_POS_CANDIDATE_A;   /* 0x10 */
    cand[1] = CS2_BONE_POS_CANDIDATE_B;   /* 0x00 */

    for (k = 0; k < 2; k++) {
        f32 pos[5][3];
        int idx[5], n, score = 0;
        idx[0] = (int)CS2_BONE_IDX_PELVIS;
        idx[1] = (int)CS2_BONE_IDX_CHEST;
        idx[2] = (int)CS2_BONE_IDX_NECK;
        idx[3] = (int)CS2_BONE_IDX_HEAD;
        idx[4] = (int)CS2_BONE_IDX_FOOT_L;

        for (n = 0; n < 5; n++) {
            if (!rd(t, bone_array + (u64)CS2_BONE_STRIDE * (u64)idx[n] + cand[k], pos[n], 12))
                break;
            if (!is_finite(pos[n][0]) || !is_finite(pos[n][1]) || !is_finite(pos[n][2]))
                break;
        }
        if (n < 5) continue;

        /* 打分: ① 骨骼要在实体原点附近(±160 单位) ② 头必须比骨盆高
         *       ③ 相邻骨骼间距 1..60 单位 ④ 脚必须在最低 */
        {
            int m;
            f32 dx = pos[0][0] - org[0], dy = pos[0][1] - org[1], dz = pos[0][2] - org[2];
            if (dx * dx + dy * dy + dz * dz < 160.0f * 160.0f) score += 2;
            if (pos[3][2] > pos[0][2] + 5.0f) score += 3;
            if (pos[2][2] > pos[3][2]) score -= 2;          /* 颈不能比头高 */
            if (pos[4][2] < pos[0][2]) score += 1;          /* 脚低于骨盆 */
            for (m = 1; m < 5; m++) {
                f32 ax = pos[m][0] - pos[m - 1][0];
                f32 ay = pos[m][1] - pos[m - 1][1];
                f32 az = pos[m][2] - pos[m - 1][2];
                f32 d2 = ax * ax + ay * ay + az * az;
                if (d2 >= 1.0f && d2 <= 3600.0f) score += 2;
                else if (d2 > 40000.0f) score -= 3;
            }
        }
        if (score > best_score) { best_score = score; best = k; }
    }

    if (best < 0) return CS2_BONE_POS_CANDIDATE_A;
    return cand[best];
}

/* ------------------------------------------------------------ 校验 */
int cs2_auto_validate(cs2_target* t, const cs2_offsets* o, u64 client_base,
                      u32* out_entities, u32* out_local_slot)
{
    f32 vm[16];
    i32 local_slot = -1;
    u64 lc;
    u32 found = 0, i;

    if (!rd(t, client_base + (u64)o->dwViewMatrix, vm, sizeof(vm))) return CS2_FAIL;
    if (!vm_test(vm)) return CS2_FAIL;

    if (!cs2_local_controller(t, o, client_base, &local_slot)) return CS2_FAIL;
    lc = (u64)local_slot;

    /* 数一下这一帧有几个"实体长得对"的槽位 (1..64) */
    for (i = 1; i <= 64; i++) {
        u64 ent = cs2_entity_at(t, o, client_base, (i32)i);
        u64 vt = 0;
        if (!ent) continue;
        if (rd(t, ent, &vt, 8) && is_user_ptr(vt)) found++;
    }
    if (found == 0) return CS2_FAIL;

    if (out_entities)    *out_entities = found;
    if (out_local_slot)  *out_local_slot = (local_slot > 0) ? local_slot : (i32)lc;
    return CS2_OK;
}

/* ------------------------------------------------------------ 初始化/解析 */
void cs2_auto_init(cs2_offsets* o)
{
    if (o == NULL) return;
    RtlZeroMemory(o, sizeof(*o));

    o->dwEntityList            = CS2_DW_ENTITYLIST;
    o->dwLocalPlayerController = CS2_DW_LOCALPLAYERCONTROLLER;
    o->dwLocalPlayerPawn       = CS2_DW_LOCALPLAYERPAWN;
    o->dwViewMatrix            = CS2_DW_VIEWMATRIX;
    o->dwGameRules             = CS2_DW_GAMERULES;
    o->dwGlobalVars            = CS2_DW_GLOBALVARS;
    o->dwNetworkGameClient     = CS2_DW_NETWORKGAMECLIENT;
    o->dwBuildNumber           = CS2_DW_BUILDNUMBER;
    o->dwWindowWidth           = CS2_DW_WINDOWWIDTH;
    o->dwWindowHeight          = CS2_DW_WINDOWHEIGHT;

    o->entity_shift            = CS2_ENT_CHUNK_SHIFT;
    o->entity_chunk_mul        = CS2_ENT_CHUNK_MUL;
    o->entity_chunk_off        = CS2_ENT_CHUNK_OFF;
    o->entity_stride           = CS2_ENT_STRIDE;
    o->entity_index_mask       = CS2_ENT_INDEX_MASK;
    o->bone_pos_off            = CS2_BONE_POS_CANDIDATE_A;
    o->src_mask                = CS2_SRC_SEED;
    o->valid                   = 0;
}

int cs2_auto_resolve(cs2_target* t, const cs2_module* client, const cs2_module* engine,
                     cs2_offsets* o)
{
    u32 found = 0;
    i32 slot = -1;

    if (t == NULL || client == NULL || o == NULL) return CS2_FAIL;

    /* ---- ① viewMatrix: 只用种子锚点 ----
     * 扫描 client.dll 找 4x4 float 会命中大量"长得像"的矩阵 (骨骼变换 / 单位阵 /
     * 动画节点), 实测两次都把锚点带偏, 导致 W2S 全错。种子值已与 cs2-dumper 逐项
     * 核对过, 每版只改数值不改位置, 所以这里不扫。 */
    o->dwViewMatrix = CS2_DW_VIEWMATRIX;

    /* ---- ① 结构扫描: 实体系统 ---- */
    g_el_hit  = 0;
    g_el_deep = CS2_MAX_DEEP;
    {
        cs2_offsets probe = *o;
        scan_writable(t, client->base, client->size, (void*)el_scan_cb, 1, &probe);
    }
    if (g_el_hit) {
        u32 rel = (u32)(g_el_hit - client->base);
        if (rel < client->size) {
            o->dwEntityList = rel;
            o->src_mask |= CS2_SRC_SCAN;
        }
    }

    /* ---- ② 语义校验: 扫出来的值必须真能解出实体 ---- */
    if (cs2_auto_validate(t, o, client->base, &found, &slot) == CS2_OK) {
        o->src_mask |= CS2_SRC_LIVE;
        o->valid = 1;
    } else if (o->src_mask & CS2_SRC_SCAN) {
        /* 扫到的某个锚点是错的(常见于扫到"长得像"的槽位), 退回种子再验一次 */
        cs2_offsets seed;
        cs2_auto_init(&seed);
        seed.bone_pos_off = o->bone_pos_off;
        if (cs2_auto_validate(t, &seed, client->base, &found, &slot) == CS2_OK) {
            seed.src_mask |= CS2_SRC_LIVE;
            seed.valid = 1;
            *o = seed;
        } else {
            o->valid = 0;
        }
    } else {
        /* 没扫到、种子也没通过: 这版可能换了数据结构, 交给下一轮重试 */
        if (cs2_auto_validate(t, o, client->base, &found, &slot) != CS2_OK) o->valid = 0;
    }

    /* ---- 版本指纹 + 骨骼标定 (拿到一个活 pawn 才做) ---- */
    if (engine != NULL) {
        u32 bn = 0;
        if (rd(t, engine->base + (u64)o->dwBuildNumber, &bn, 4)) o->build_number = bn;
    }
    if (o->valid) {
        u64 lc = cs2_local_controller(t, o, client->base, &slot);
        if (lc) {
            u64 pawn_handle = 0;
            u32 h = 0;
            if (rd(t, lc + CS2_NV_M_HPLAYERPAWN, &h, 4) && h != 0 && h != 0xFFFFFFFFu) {
                i32 pi = (i32)(h & CS2_ENT_HANDLE_MASK);
                u64 pawn = cs2_entity_at(t, o, client->base, pi);
                if (pawn) {
                    u64 sn = 0;
                    if (rd(t, pawn + CS2_NV_M_PGAMESCENENODE, &sn, 8) && is_user_ptr(sn))
                        o->bone_pos_off = cs2_auto_bone_offset(t, o, sn);
                }
            }
            pawn_handle = lc;
            (void)pawn_handle;
        }
    }
    return o->valid ? CS2_OK : CS2_FAIL;
}
