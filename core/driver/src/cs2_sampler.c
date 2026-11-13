/* ============================================================================
 * cs2core.sys —— 唯一职责: 读游戏内存 -> 写定长帧。不做任何派生计算。
 *
 * 帧里只有原始量: 位置 / 眼位 / 角度 / 血量 / 队伍 / 骨骼 / 武器 / 距离 / 视野(FOV)。
 * view matrix 由组件用 眼睛+角度+FOV 自己算，驱动不参与。
 * ========================================================================== */
#include "cs2_sampler.h"
#include "cs2_auto.h"
#include "cs2_channel.h"
#include "cs2_file.h"
#include "cs2_shm.h"
#include "cs2_strs.h"    /* 目标/模块名密文表 */
#include "cs2_lic.h"
#include "cs2_env.h"
#include "cs2_hide.h"
#include "cs2_ptread.h"
#include "../../../shared/cs2env_abi.h"
#include "../inc/cs2core_km.h"

#define CS2_UNITS_TO_M      0.019051f   /* 1 CS2 unit = 1.905 cm -> 52.49 units/m */

/* ntifs.h 在这个 WDK 版本里没有导出原型；以前靠隐式 int 声明在赌 ABI。
 * 实际返回 ULONG（32 位），显式声明一次，编译器就不会再猜。 */
ULONG PsGetProcessSessionId(PEPROCESS Process);
#define CS2_MAX_CONSEC_FAIL 240        /* 杩炵画璇诲け璐ヨ繖涔堝娆″氨璁や负娓告垙閫€鍑轰�?*/
#define CS2_RESCAN_AFTER    30         /* 鏍￠獙澶辫触杩欎箞澶氬抚鍚庨噸鏂拌В鏋愬亸�?*/

/* ABI 甯у唴鍐欏師璇?*/
static void wr_u32(u8* f, u32 off, u32 v)  { *(u32*)(f + off) = v; }
static void wr_i32(u8* f, u32 off, i32 v)  { *(i32*)(f + off) = v; }
static void wr_u64(u8* f, u32 off, u64 v)  { *(u64*)(f + off) = v; }
static void wr_u16(u8* f, u32 off, u16 v)  { *(u16*)(f + off) = v; }
static void wr_u8 (u8* f, u32 off, u8  v)  { *(u8* )(f + off) = v; }
static void wr_f32(u8* f, u32 off, f32 v)  { *(f32*)(f + off) = v; }
static void wr_vec3(u8* f, u32 off, const f32* v) { wr_f32(f, off, v[0]); wr_f32(f, off + 4, v[1]); wr_f32(f, off + 8, v[2]); }

static u32 fnv1a(const u8* p, u32 len)
{
    u32 h = 0x811C9DC5u;
    u32 i;
    for (i = 0; i < len; i++) { h ^= (u32)p[i]; h *= 0x01000193u; }
    return h;
}

/* --------------------------------------------------------------- 閲囨牱涓婁笅�?*/
typedef struct cs2_ctx {
    cs2_target   t;
    cs2_module   client;
    cs2_module   engine;
    cs2_offsets  off;
    int          opened;
    u32          hz;
    u32          consec_fail;
    u32          valid_fail;
    u32          seq;
    /* 骨骼布局标定：CTransform 的 stride / 位置字段偏移在版本间会变。
     * 标定不出来就宁可写 0（组件会退回"碰撞盒/身高"方框），也不要把四元数当坐标画出来。 */
    u32          bone_stride;
    u32          bone_pos;
    u32          bone_calib;
    /* 腿部槽位运行期自愈：CS2 换模型/换 agent 后原地索引会漂移，
     * 典型现象是"小腿画得出来、大腿整段消失"。见 bone_map_legs()。 */
    u32          bone_map;
    u8           idx_hip_l, idx_knee_l, idx_foot_l;
    u8           idx_hip_r, idx_knee_r, idx_foot_r;
    cs2_sampler_stats st;

    /* 鏈湴鐜╁缂撳�?*/
    i32          local_slot;
    u32          local_team;
    f32          local_org[3];
    f32          local_fov;

    u8*          frame;      /* 褰撳墠姝ｅ湪鎷肩殑甯?(鏈湴缂撳啿, 鍙戝竷鏃舵暣鍧楁嫹杩?section) */

    /* 鐩爣杩涚▼鎵€鍦ㄤ細璇?id: �?section 蹇呴』寤哄湪 \Sessions\<id>\BaseNamedObjects
     * �? 缁勪�?AppContainer)鎵嶈�?Local\ 杩欎釜鍚嶅瓧銆傚缓鍦ㄥ叏灞€�?0)鏃剁粍浠堕偅�?
     * OpenFileMapping 鍙細鎷垮埌 ERROR_FILE_NOT_FOUND�?*/
    u32          session;
} cs2_ctx;

/* v1.4.8: per-slot caches.
 *
 * Why: the sampler used to re-read 17 bone positions + name + weapon detail for
 * every player on every tick -- ~26 MmCopyVirtualMemory calls per player, ~260
 * per frame with 10 players. That is what capped the pipeline at 8-9 Hz.
 * Now the cheap data (position/health/team/flags/viewmatrix) is read every tick
 * and the expensive data is refreshed round-robin, copied from cache in between.
 * Bones: every 4th tick per slot.  Name: only when the controller changes.
 * Weapon/armor detail: every 8th tick per slot.
 */
typedef struct {
    u32  bone_valid;
    f32  bone[CS2CORE_MAX_BONES][3];
    u64  name_ctrl;
    char name[CS2CORE_NAME_LEN];
    u32  w_tick;
    u16  weapon, ammo;
    u8   gear;
} slot_cache;

static slot_cache g_sc[CS2CORE_MAX_PLAYERS];
static u32 g_tick;

static cs2_ctx g_ctx;
/* v1.4: 诊断 section / lic section 全部删除 -> 运行期零命名对象 */
static KEVENT  g_stop_ev;
static KEVENT  g_worker_done_ev;
static volatile LONG g_worker_queued;
static volatile LONG g_running;

/* ------------------------------------------------------------ 杩涚�?妯″潡 */
static int ctx_open(cs2_ctx* c)
{
    if (c->opened) return CS2_OK;
    RtlZeroMemory(&c->t, sizeof(c->t));
    if (cs2_target_open(&c->t) != STATUS_SUCCESS) {
        c->st.last_error = 1;
        return CS2_FAIL;
    }
    RtlZeroMemory(&c->client, sizeof(c->client));
    RtlZeroMemory(&c->engine, sizeof(c->engine));
    cs2_module_find(&c->t, cs2_s_client(), &c->client);
    cs2_module_find(&c->t, cs2_s_engine(), &c->engine);
    if (c->client.base == 0) {
        cs2_target_close(&c->t);
        c->st.last_error = 1;
        return CS2_FAIL;
    }
    c->opened = 1;
    c->valid_fail = 0;
    c->consec_fail = 0;

    /* 鐩爣杩涚▼鐨勪細璇?id: �?section 瑕佸缓鍦ㄨ繖涓細璇濈殑 BaseNamedObjects �?*/
    c->session = 0;
    if (c->t.process != NULL) {
        c->session = PsGetProcessSessionId(c->t.process);
    }

    cs2_auto_init(&c->off);
    c->bone_stride = CS2_BONE_STRIDE;
    c->bone_pos    = CS2_BONE_POS_CANDIDATE_A;
    c->bone_calib  = 0;
    c->bone_map    = 0;
    c->idx_hip_l   = CS2_BONE_IDX_HIP_L;
    c->idx_knee_l  = CS2_BONE_IDX_KNEE_L;
    c->idx_foot_l  = CS2_BONE_IDX_FOOT_L;
    c->idx_hip_r   = CS2_BONE_IDX_HIP_R;
    c->idx_knee_r  = CS2_BONE_IDX_KNEE_R;
    c->idx_foot_r  = CS2_BONE_IDX_FOOT_R;
    cs2_auto_init(&c->off);
    cs2_auto_resolve(&c->t, &c->client, &c->engine, &c->off);
    /* 偏移来自 cs2-dumper 的精确 dump，不需要"校验"来确认；
     * 校验依赖游戏矩阵，而矩阵在窗口化等状态下会退化，导致永远停在阶段 9。 */
    c->off.valid = 1;
    c->st.rescan_count++;
    c->st.build_number = c->off.build_number;
    return CS2_OK;
}

static void ctx_close(cs2_ctx* c)
{
    if (c->opened) {
        cs2_target_close(&c->t);
        c->opened = 0;
    }
    c->st.last_error = 1;
}

/* ---------------------------------------------------------------- 璇诲悕�?*/
static void read_name(cs2_target* t, u64 controller, char* dst, u32 cap)
{
    char raw[CS2CORE_NAME_LEN * 2];
    u32 i;
    RtlZeroMemory(dst, cap);
    RtlZeroMemory(raw, sizeof(raw));
    if (controller == 0) return;
    if (cs2_read(t, controller + CS2_NV_M_ISZPLAYERNAME, raw, sizeof(raw)) != CS2_OK) return;
    for (i = 0; i + 1 < cap && i < sizeof(raw); i++) {
        char ch = raw[i];
        if (ch == 0) break;
        if (ch < 0x20 || ch > 0x7E) { ch = '?'; }
        dst[i] = ch;
    }
    dst[i] = 0;
}

/* ------------------------------------------------------ 璇昏澶囦綅 (�?闀?鎷嗗�?
 * 姝﹀�?ID / 寮瑰�?/ 鎹㈠�?闇€瑕佹鍣ㄥ疄�? �?read_weapon_detail (瑕佸疄浣撹В寮曠敤) */
static void read_gear(cs2_target* t, u64 pawn, u8* gear_out, u16* weapon_out,
                      u16* ammo_out, u8* armor_out)
{
    u8  gear = 0;
    u8  armor = 0;
    i32 armorv = 0;
    u8  scoped = 0, defusing = 0, helmet = 0, defuser = 0;

    if (weapon_out) *weapon_out = 0;
    if (ammo_out)   *ammo_out = 0;
    if (armor_out)  *armor_out = 0;
    if (gear_out)   *gear_out = 0;

    if (cs2_read(t, pawn + CS2_NV_M_ARMORVALUE, &armorv, 4) == CS2_OK) {
        if (armorv < 0) armorv = 0;
        if (armorv > 100) armorv = 100;
        armor = (u8)armorv;
        if (armor > 0) gear |= CS2_GEAR_ARMORED;
    }
    if (cs2_read(t, pawn + CS2_NV_M_BISSCOPED, &scoped, 1) == CS2_OK && scoped)
        gear |= CS2_GEAR_ZOOMED;
    if (cs2_read(t, pawn + CS2_NV_M_BISDEFUSING, &defusing, 1) == CS2_OK && defusing)
        gear |= CS2_GEAR_DEFUSING;
    /* 澶寸�?鎷嗗脊鍣ㄦ寕鍦ㄦ帶鍒跺櫒�? 鐢辫皟鐢ㄦ柟�?(�?sample_frame 閲岀�?ctrl �? */
    (void)helmet; (void)defuser;

    if (armor_out)  *armor_out = armor;
    if (gear_out)   *gear_out = gear;
}

/* 鎷垮埌姝﹀櫒瀹炰綋鍚庤ˉ姝﹀�?ID / 寮硅�?/ 鎹㈠�?(鍗曠嫭涓€娈? 鍥犱负瀹冭澶氫竴娆″疄浣撹В寮曠�? */
static void read_weapon_detail(cs2_target* t, const cs2_offsets* o, u64 client_base,
                              u64 pawn, u16* weapon_out, u16* ammo_out, u8* gear_out)
{
    u64 wsp = 0;
    u32 handle = 0;
    u64 weapon = 0;
    u64 attr = 0;
    u16 wid = 0;
    i32 clip = 0;
    u8 reload = 0;

    if (cs2_read(t, pawn + CS2_NV_M_PWEAPONSERVICES, &wsp, 8) != CS2_OK || wsp < 0x10000ull)
        return;
    if (cs2_read(t, wsp + 0x60, &handle, 4) != CS2_OK) return;
    if (handle == 0 || handle == 0xFFFFFFFFu) return;

    weapon = cs2_entity_at(t, o, client_base, (i32)(handle & CS2_ENT_HANDLE_MASK));
    if (weapon == 0) return;

    if (cs2_read(t, weapon + CS2_NV_M_ATTRIBUTEMANAGER + CS2_NV_M_ITEM +
                    CS2_NV_M_ITEMDEFINITIONINDEX, &wid, 2) == CS2_OK) {
        if (weapon_out) *weapon_out = wid;
    }
    if (cs2_read(t, weapon + CS2_NV_M_ICLIP1, &clip, 4) == CS2_OK) {
        if (ammo_out) *ammo_out = (u16)(clip < 0 ? 0 : (clip > 0xFFFF ? 0xFFFF : clip));
    }
    if (cs2_read(t, weapon + CS2_NV_M_BINRELOAD, &reload, 1) == CS2_OK && reload) {
        if (gear_out) *gear_out |= CS2_GEAR_RELOADING;
    }
    (void)attr;
}

/* ---------------------------------------- 甯у唴瀹炰綋閾剧紦�?(鎬ц兘鍏抽敭璺緞)
 * 姣忓抚鍙涓€�?dwEntityList, 浠ュ強姣忎釜 chunk 鎸囬拡涓€娆°�?
 * 鍘熷疄鐜版瘡瑙ｅ紩鐢ㄤ竴涓疄浣撻兘瑕佽�?2 �?entityList + chunk), 涓€涓帺瀹惰鐢?3 �?
 * (鎺у埗�?/ pawn / 姝﹀�?, 10 涓帺瀹跺氨鏄?60 娆＄函鍐椾綑�?鈥斺�?杩欐槸鍙戝竷棰戠巼涓婁笉鍘荤�?
 * 涓诲洜涔嬩竴(�?docs/PERF.md)�?*/
static u64 g_list_base;
static u64 g_chunk[8];
static u8  g_chunk_ok[8];
static int g_cache_on;

static void ent_cache_begin(void)
{
    g_cache_on = 1;
    g_list_base = 0;
    RtlZeroMemory(g_chunk_ok, sizeof(g_chunk_ok));
    RtlZeroMemory(g_chunk, sizeof(g_chunk));
}

static u64 ent_at(cs2_target* t, const cs2_offsets* o, u64 client_base, i32 index)
{
    u32 shift = o->entity_shift ? o->entity_shift : CS2_ENT_CHUNK_SHIFT;
    u32 mul   = o->entity_chunk_mul ? o->entity_chunk_mul : CS2_ENT_CHUNK_MUL;
    u32 coff  = o->entity_chunk_off ? o->entity_chunk_off : CS2_ENT_CHUNK_OFF;
    u32 strd  = o->entity_stride ? o->entity_stride : CS2_ENT_STRIDE;
    u32 imask = o->entity_index_mask ? o->entity_index_mask : CS2_ENT_INDEX_MASK;
    u32 ci;
    u64 chunk, ent = 0;

    if (index <= 0) return 0;
    if (!g_cache_on) return cs2_entity_at(t, o, client_base, index);

    if (g_list_base == 0) {
        if (!cs2_read(t, client_base + (u64)o->dwEntityList, &g_list_base, 8)) return 0;
        if (g_list_base < 0x10000ull) { g_list_base = 0; return 0; }
    }

    ci = (u32)((u64)index >> shift);
    if (ci >= 8u) return 0;
    if (!g_chunk_ok[ci]) {
        if (!cs2_read(t, g_list_base + (u64)mul * (u64)ci + (u64)coff, &g_chunk[ci], 8)) return 0;
        if (g_chunk[ci] < 0x10000ull) { g_chunk[ci] = 0; return 0; }
        g_chunk_ok[ci] = 1;
    }
    chunk = g_chunk[ci];
    if (chunk == 0) return 0;
    if (!cs2_read(t, chunk + (u64)strd * ((u64)index & imask), &ent, 8)) return 0;
    return (ent > 0x10000ull) ? ent : 0;
}

/* ------------------------------------------------------------------ 缁勫�?*/
/* ---- build 14192 �� dwViewMatrix ���˻����������ñ�����ҵ��۾�λ��+�Ƕ��Լ��� ---- */
static f32 ksin(f32 x)
{
    while (x >  3.14159265f) { x -= 6.28318531f; }
    while (x < -3.14159265f) { x += 6.28318531f; }
    {
        f32 x2 = x * x;
        return x * (1.0f - x2 / 6.0f + x2 * x2 / 120.0f - x2 * x2 * x2 / 5040.0f);
    }
}
static f32 kcos(f32 x) { return ksin(x + 1.57079633f); }

static f32 ksqrtf(f32 x)
{
    f32 r;
    int it;
    if (!(x > 0.0f)) { return 0.0f; }
    r = x * 0.5f;
    for (it = 0; it < 16; it++) { r = 0.5f * (r + x / r); }
    return r;
}
static void build_view_matrix(f32* m, f32 ex, f32 ey, f32 ez, f32 pitch_deg, f32 yaw_deg,
                             f32 invTanH, f32 invTanV, f32 invZ)
{
    f32 pitch = -pitch_deg * 0.0174532925f;
    f32 yaw   = yaw_deg * 0.0174532925f;   /* 全局 dwViewAngles 用原始符号：取反会让相机朝背面，w<0 全部投不出来 */
    f32 cp = kcos(pitch), sp = ksin(pitch);
    f32 cy = kcos(yaw),   sy = ksin(yaw);
    f32 fx = cp * cy, fy = cp * sy, fz = -sp;
    f32 rx = sy,      ry = -cy,     rz = 0.0f;   /* right = forward x up = (sy,-cy,0) */
    f32 ux = ry * fz - rz * fy;
    f32 uy = rz * fx - rx * fz;
    f32 uz = rx * fy - ry * fx;

    /* 视野缩放直接沿用游戏自己的投影行长度：这样开镜/变倍时框会跟着 FOV 变化，
     * 远处也不会再被放大（之前用写死的 1.0，等于永远按 90 度算）。 */
    if (!(invTanH > 1e-4f) || !(invTanV > 1e-4f)) { invTanH = 1.0f; invTanV = 1.0f; }
    if (!(invZ    > 1e-4f)) { invZ = 1.0f; }

    m[0]  =  rx * invTanH; m[1]  =  ry * invTanH; m[2]  =  rz * invTanH;
    m[3]  = -(rx * ex + ry * ey + rz * ez) * invTanH;
    m[4]  =  ux * invTanV; m[5]  =  uy * invTanV; m[6]  =  uz * invTanV;
    m[7]  = -(ux * ex + uy * ey + uz * ez) * invTanV;
    m[8]  =  fx * invZ;    m[9]  =  fy * invZ;    m[10] =  fz * invZ;
    m[11] = -(fx * ex + fy * ey + fz * ez) * invZ;
    m[12] =  fx * invZ;    m[13] =  fy * invZ;    m[14] =  fz * invZ;
    m[15] =  m[11];
}

/* ==========================================================================
 * ABI v6 tail: per-player world AABB + ground item list
 *
 * Strictly additive: writes only into frame[CS2_FRAME_SIZE .. CS2_FRAME_SIZE_V2).
 * Any failed read just clears a mask bit / drops an item, so the reader
 * degrades to "no bounds / no items" instead of drawing garbage.
 * ======================================================================== */
#define CS2_ITEM_SCAN_MAX 768          /* entity indices scanned for items */
static u32 g_tail_items;               /* items cached from the last scan tick */
static u8  g_grnd_saved[CS2_GRND_TAIL_SIZE];   /* 上一次投掷物扫描的整块尾部 */

static u8 item_cat_of(u16 id)
{
    if (id == 49) return CS2_ITEM_CAT_C4;
    if (id == 55) return CS2_ITEM_CAT_KIT;
    if (id >= 43 && id <= 48) return CS2_ITEM_CAT_GRENADE;
    if (id == 57) return CS2_ITEM_CAT_OTHER;
    return CS2_ITEM_CAT_WEAPON;
}

/* Reject NaN/Inf and absurd coordinates without <math.h>. */
static int item_vec_ok(const f32* p)
{
    if (!(p[0] > -32768.0f && p[0] < 32768.0f)) return 0;
    if (!(p[1] > -32768.0f && p[1] < 32768.0f)) return 0;
    if (!(p[2] > -32768.0f && p[2] < 32768.0f)) return 0;
    return 1;
}

/* Scan the entity list for dropped weapons / grenades / C4 / defuse kits.
 * Runs every 8th tick only (~15Hz at 120Hz sampling) and is capped at
 * CS2_ITEM_SCAN_MAX indices; each index costs 1-2 reads through the cached
 * chunk table, so the added cost stays in the tens of reads per tick. */
static int item_is_zero(const f32* p)
{
    return (p[0] > -1.0f && p[0] < 1.0f && p[1] > -1.0f && p[1] < 1.0f &&
            p[2] > -1.0f && p[2] < 1.0f) ? 1 : 0;
}

static u32 scan_items(cs2_ctx* c, u64 client_base, u8* tail)
{
    u32 n = 0, idx;
    for (idx = 1; idx < CS2_ITEM_SCAN_MAX && n < CS2CORE_ITEM_MAX; idx++) {
        u64 ent;
        u16 id = 0, ammo = 0;
        u32 owner = 0;
        u64 scene = 0;
        f32 pos[3];
        u8* rec;
        u8 cat, flags = 0;
        int have_pos = 0;

        ent = ent_at(&c->t, &c->off, client_base, (i32)idx);
        if (!ent) continue;

        if (!cs2_read(&c->t, ent + CS2_NV_M_ATTRIBUTEMANAGER + CS2_NV_M_ITEM +
                            CS2_NV_M_ITEMDEFINITIONINDEX, &id, 2)) continue;
        if (id == 0 || id > 1000) continue;
        if (id == 42 || id == 59 || (id >= 500 && id <= 600)) continue;   /* knives/gloves */

        /* ---- 归属判定（宽松版）------------------------------------------------
         * 以前只要 m_hOwnerEntity 读出来"不是 0 / 不是 0x7FFF"就当成"有人拿着"跳过，
         * 偏移一旦漂移就会读到别的字段（指针、时间戳…），结果**一件物品都不显示**。
         * 现在只有当 owner 句柄能解析出实体、而且那个实体是活着的玩家时才跳过；
         * 解析不出来就按"掉在地上"处理 —— 宁可多画一件，也不要整片看不见。 */
        if (cs2_read(&c->t, ent + CS2_NV_M_HOWNERENTITY, &owner, 4)) {
            u32 hh = owner & CS2_ENT_HANDLE_MASK;
            if (hh != 0 && hh != CS2_ENT_HANDLE_MASK) {
                u64 own_ent = ent_at(&c->t, &c->off, client_base, (i32)hh);
                if (own_ent) {
                    u8 own_life = 1;
                    i32 own_hp = 0;
                    (void)cs2_read(&c->t, own_ent + CS2_NV_M_LIFESTATE, &own_life, 1);
                    (void)cs2_read(&c->t, own_ent + CS2_NV_M_IHEALTH, &own_hp, 4);
                    if (own_life == 0 && own_hp > 0) { continue; }   /* 真有人在拿 */
                }
            }
        } else {
            flags |= CS2_ITEM_F_ON_GROUND;
        }

        /* ---- 位置：三条路依次试 ----------------------------------------------
         *   1) m_pGameSceneNode -> m_vecAbsOrigin（地面物品正常路径）
         *   2) 直接读实体 +m_vecAbsOrigin（有些物品没有 scene node）
         *   3) 读实体 +m_vecOrigin（老字段，留作最后兜底）
         * 只要有一条读出非 (0,0,0) 的合理坐标就收。 */
        if (cs2_read(&c->t, ent + CS2_NV_M_PGAMESCENENODE, &scene, 8) &&
            scene > 0x10000ull &&
            cs2_read(&c->t, scene + CS2_NV_M_VECABSORIGIN, pos, 12) &&
            item_vec_ok(pos) && !item_is_zero(pos)) {
            have_pos = 1;
        }
        if (!have_pos &&
            cs2_read(&c->t, ent + CS2_NV_M_VECABSORIGIN, pos, 12) &&
            item_vec_ok(pos) && !item_is_zero(pos)) {
            have_pos = 1;
        }
        if (!have_pos &&
            cs2_read(&c->t, ent + CS2_NV_M_VECORIGIN, pos, 12) &&
            item_vec_ok(pos) && !item_is_zero(pos)) {
            have_pos = 1;
        }
        if (!have_pos) continue;

        cat = item_cat_of(id);
        if (cat == CS2_ITEM_CAT_KIT) flags |= CS2_ITEM_F_DEFUSE_KIT;
        if (cat == CS2_ITEM_CAT_WEAPON) {
            i32 clip = 0;
            if (cs2_read(&c->t, ent + CS2_NV_M_ICLIP1, &clip, 4) && clip > 0 && clip < 1000) {
                ammo = (u16)clip;
                flags |= CS2_ITEM_F_HAS_AMMO;
            }
        }
        if (cat == CS2_ITEM_CAT_C4) { flags |= CS2_ITEM_F_PLANTED; }

        rec = tail + CS2_OFF_T_ITEMS + n * CS2CORE_ITEM_SIZE;
        RtlZeroMemory(rec, CS2CORE_ITEM_SIZE);
        *(f32*)(rec + CS2_OFF_I_POS + 0) = pos[0];
        *(f32*)(rec + CS2_OFF_I_POS + 4) = pos[1];
        *(f32*)(rec + CS2_OFF_I_POS + 8) = pos[2];
        *(u16*)(rec + CS2_OFF_I_ID)      = id;
        rec[CS2_OFF_I_CAT]   = cat;
        rec[CS2_OFF_I_FLAGS] = flags;
        *(u16*)(rec + CS2_OFF_I_AMMO)    = ammo;
        n++;
    }
    return n;
}

/* Tail header + checksum. AABB entries were filled by the player loop
 * (they need pawn/scene, which are out of scope here). */

/* --------------------------------------------------------------------------
 * 骨骼布局标定
 *
 * 为什么要标定：骨骼是 CTransform 数组（{Quaternion(pad16), Vector pos, float scale}），
 * 但数组起始指针的落点 / stride / 位置字段偏移在更新后都可能变。
 * 读错时的典型现象就是"坐标全在 0~1 之间"（实际读到的是四元数）——组件那边
 * 会因为身高不合理而不画骨骼，屏幕上就只剩方框。
 *
 * 判据只用几何不变量（与地图/单位无关）：
 *   头骨   = 脚底 + 45~95 单位
 *   盆骨   = 脚底 + 15~60 单位
 *   头-盆骨距离 = 14~50 单位
 * 选得分最高的一组 (stride, posOff) 缓存起来，每进程只做一次。
 * ------------------------------------------------------------------------ */
static int bone_vec_ok(const f32* p)
{
    if (!(p[0] > -32768.0f && p[0] < 32768.0f)) return 0;
    if (!(p[1] > -32768.0f && p[1] < 32768.0f)) return 0;
    if (!(p[2] > -32768.0f && p[2] < 32768.0f)) return 0;
    return 1;
}

static int bone_calibrate(cs2_ctx* c, u64 bone_array, const f32* org)
{
    static const u32 strides[3] = { CS2_BONE_STRIDE, 0x30u, 0x40u };
    static const u32 offs[4]    = { CS2_BONE_POS_CANDIDATE_A, CS2_BONE_POS_CANDIDATE_B, 0x20u, 0x30u };
    i32 best = 0x7FFFFFFF;
    u32 bs = 0, bo = 0;
    u32 si, oi;

    for (si = 0; si < 3; si++) {
        for (oi = 0; oi < 4; oi++) {
            f32 head[3], pelvis[3];
            f32 dzH, dzP, dx, dy, dz, d2;
            i32 score;

            if (!cs2_read(&c->t, bone_array + (u64)strides[si] * CS2_BONE_IDX_HEAD + (u64)offs[oi], head, 12)) continue;
            if (!cs2_read(&c->t, bone_array + (u64)strides[si] * CS2_BONE_IDX_PELVIS + (u64)offs[oi], pelvis, 12)) continue;
            if (!bone_vec_ok(head) || !bone_vec_ok(pelvis)) continue;

            dzH = head[2] - org[2];
            dzP = pelvis[2] - org[2];
            if (dzH < 45.0f || dzH > 95.0f) continue;
            if (dzP < 15.0f || dzP > 60.0f) continue;

            dx = head[0] - pelvis[0];
            dy = head[1] - pelvis[1];
            dz = head[2] - pelvis[2];
            d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < 196.0f || d2 > 2500.0f) continue;      /* 14 .. 50 单位 */

            score = (i32)((dzH - 68.0f) * (dzH - 68.0f) + (dzP - 36.0f) * (dzP - 36.0f));
            if (score < best) { best = score; bs = strides[si]; bo = offs[oi]; }
        }
    }

    if (bs != 0) {
        c->bone_stride = bs;
        c->bone_pos    = bo;
        c->bone_calib  = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * 腿部槽位自愈
 *
 * Why: 骨骼表是 per-model 的。换 agent / 换模型后 leg_upper_L/R 的索引会整体
 * 平移；索引错一格时"小腿还在、大腿整段消失"——因为 骨盆→髋 还画得出来，
 * 髋→膝 读到的却是下一层骨骼，世界长度对不上、组件把整段丢了。
 *
 * 判据只用几何不变量（与模型/地图无关）:
 *   髋 = 骨盆下方 0..24 单位、水平偏移 >= 1.5
 *   膝 = 髋下方 >= 5 单位、髋膝间距 9..36
 * 索引不猜: 以已知可信的 骨盆/膝 为锚点，在 0..31 里找最像髋的那根。
 * 找不到就保留种子值 —— 组件侧还有"骨盆→膝"兜底，不会画出蜘蛛网。
 * ---------------------------------------------------------------------- */
static f32 bone_absf(f32 v) { return v < 0.0f ? -v : v; }

static int bone_hip_plausible(const f32* pelvis, const f32* hip, const f32* knee)
{
    f32 dzp, dzk, dx, dy, dz, d2, h2;
    if (!bone_vec_ok(hip)) return 0;
    if (hip[0] == 0.0f && hip[1] == 0.0f && hip[2] == 0.0f) return 0;

    dzp = pelvis[2] - hip[2];
    dzk = hip[2] - knee[2];
    if (dzp < 0.0f || dzp > 24.0f) return 0;
    if (dzk < 5.0f) return 0;

    dx = hip[0] - knee[0]; dy = hip[1] - knee[1]; dz = hip[2] - knee[2];
    d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < 81.0f || d2 > 1296.0f) return 0;          /* 9 .. 36 单位 */

    dx = hip[0] - pelvis[0]; dy = hip[1] - pelvis[1];
    h2 = dx * dx + dy * dy;
    if (h2 < 2.25f || h2 > 676.0f) return 0;           /* 水平 1.5 .. 26 单位 */
    return 1;
}

static u8 bone_find_hip(cs2_ctx* c, u64 bone_array, const f32* pelvis, const f32* knee, u8 seed)
{
    u8  best = seed;
    f32 best_score = 1.0e9f;
    u32 i;

    for (i = 0; i < 32u; i++) {
        f32 p[3], dx, dy, dzp, dhk, score;
        if (i == CS2_BONE_IDX_PELVIS) continue;
        if (!cs2_read(&c->t, bone_array + (u64)c->bone_stride * (u64)i + (u64)c->bone_pos, p, 12)) continue;
        if (!bone_hip_plausible(pelvis, p, knee)) continue;

        /* 期望: 髋在骨盆下 3..8 单位、髋膝间距 14..22 单位 */
        dzp = pelvis[2] - p[2];
        dx = p[0] - knee[0]; dy = p[1] - knee[1];
        dhk = bone_absf(p[2] - knee[2]) + 0.5f * bone_absf(dx) + 0.5f * bone_absf(dy);
        score = bone_absf(dzp - 6.0f) + bone_absf(dhk - 18.0f);
        if (score < best_score) { best_score = score; best = (u8)i; }
    }
    return best;
}

static void bone_map_legs(cs2_ctx* c, u64 bone_array)
{
    f32 pelvis[3], knee_l[3], knee_r[3];
    u8  new_hip_l, new_hip_r;

    if (c->bone_map || !c->bone_calib) return;

    if (!cs2_read(&c->t, bone_array + (u64)c->bone_stride * CS2_BONE_IDX_PELVIS + (u64)c->bone_pos, pelvis, 12)) return;
    if (!bone_vec_ok(pelvis) || (pelvis[0] == 0.0f && pelvis[1] == 0.0f && pelvis[2] == 0.0f)) return;

    if (!cs2_read(&c->t, bone_array + (u64)c->bone_stride * c->idx_knee_l + (u64)c->bone_pos, knee_l, 12)) return;
    if (!cs2_read(&c->t, bone_array + (u64)c->bone_stride * c->idx_knee_r + (u64)c->bone_pos, knee_r, 12)) return;
    if (!bone_vec_ok(knee_l) || !bone_vec_ok(knee_r)) return;
    if (knee_l[2] > pelvis[2] - 8.0f || knee_r[2] > pelvis[2] - 8.0f) return;

    new_hip_l = bone_find_hip(c, bone_array, pelvis, knee_l, c->idx_hip_l);
    new_hip_r = bone_find_hip(c, bone_array, pelvis, knee_r, c->idx_hip_r);

    c->idx_hip_l = new_hip_l;
    c->idx_hip_r = new_hip_r;
    c->bone_map  = 1;
}


/* ==========================================================================
 * ABI v7 尾部：投掷物 + 燃烧区
 *
 * 识别方式（不依赖 class id / 名字，只靠"结构不变量"）：
 *   投掷物：m_DmgRadius ∈ [0,2000]、m_flDetonateTime / m_flSpawnTime 都是可信的
 *           游戏时间且 detonate >= spawn-1、m_bIsLive ∈ {0,1}。
 *           随机实体上这一串同时成立的概率可以忽略。
 *   燃烧区：m_fireCount ∈ [1,64] 且 m_nInfernoType ∈ [0,2]，
 *           再读 64 个火焰点，取"正在燃烧的点"的包围圆。
 *
 * 代价控制：每 8 tick 扫一遍（和物品同频）；燃烧区每 16 tick 才探一次。
 * ========================================================================== */
static u32 g_grnd_count;

static int gren_time_ok(f32 t)
{
    return (t > 0.0f) && (t < 1.0e7f);
}

/* 从实体上拿"当前世界坐标"：scene node -> 实体自身 -> 老字段 */
static int gren_pos(cs2_ctx* c, u64 ent, f32* out)
{
    u64 scene = 0;
    if (cs2_read(&c->t, ent + CS2_NV_M_PGAMESCENENODE, &scene, 8) && scene > 0x10000ull &&
        cs2_read(&c->t, scene + CS2_NV_M_VECABSORIGIN, out, 12) && item_vec_ok(out) && !item_is_zero(out))
        return 1;
    if (cs2_read(&c->t, ent + CS2_NV_M_VECABSORIGIN, out, 12) && item_vec_ok(out) && !item_is_zero(out))
        return 1;
    return 0;
}

static u32 scan_grenades(cs2_ctx* c, u64 client_base, u8* tail2)
{
    u32 n = 0, idx;
    int probe_fire = ((g_tick & 15u) == 0u);

    for (idx = 1; idx < CS2_ITEM_SCAN_MAX && n < CS2CORE_GREN_MAX; idx++) {
        u64 ent;
        u8  gb[0x88];
        f32 dmg_r, det, spawn, dmg;
        u8  live, smoke;
        f32 ipos[3], ivel[3];
        i32 bounces;
        f32 pos[3];
        u8* rec;
        int is_proj, is_fire = 0;
        f32 fire_r = 0.0f, fire_c[3];
        u32 fire_pts = 0;
        f32 lf = 0.0f;

        ent = ent_at(&c->t, &c->off, client_base, (i32)idx);
        if (!ent) continue;

        /* ---- 投掷物 ---- */
        RtlZeroMemory(gb, sizeof(gb));
        if (!cs2_read(&c->t, ent + CS2_NV_M_BISSMOKEGRENADE, gb, sizeof(gb))) continue;

        smoke = gb[CS2_NV_M_BISSMOKEGRENADE - CS2_NV_M_BISSMOKEGRENADE];
        live  = gb[CS2_NV_M_BISLIVE - CS2_NV_M_BISSMOKEGRENADE];
        RtlCopyMemory(&dmg_r, gb + (CS2_NV_M_DMGRADIUS - CS2_NV_M_BISSMOKEGRENADE), 4);
        RtlCopyMemory(&det,   gb + (CS2_NV_M_FLDETONATETIME - CS2_NV_M_BISSMOKEGRENADE), 4);
        RtlCopyMemory(&dmg,   gb + (CS2_NV_M_FLDAMAGE - CS2_NV_M_BISSMOKEGRENADE), 4);
        RtlCopyMemory(ipos,   gb + (CS2_NV_M_VINITIALPOSITION - CS2_NV_M_BISSMOKEGRENADE), 12);
        RtlCopyMemory(ivel,   gb + (CS2_NV_M_VINITIALVELOCITY - CS2_NV_M_BISSMOKEGRENADE), 12);
        RtlCopyMemory(&bounces, gb + (CS2_NV_M_NBOUNCES - CS2_NV_M_BISSMOKEGRENADE), 4);
        RtlCopyMemory(&spawn, gb + (CS2_NV_M_FLSPAWNTIME - CS2_NV_M_BISSMOKEGRENADE), 4);

        is_proj = (smoke <= 1) && (live <= 1) &&
                  (dmg_r >= 0.0f) && (dmg_r <= 2000.0f) &&
                  gren_time_ok(det) && gren_time_ok(spawn) &&
                  (det >= spawn - 1.0f) && (det - spawn < 60.0f) &&
                  (dmg >= -1.0f) && (dmg <= 2000.0f);

        if (is_proj || probe_fire) {
            /* ---- 燃烧区（C_Inferno）---- */
            if (!is_proj && probe_fire) {
                i32 fc = 0, itype = -1;
                u8 fb[8];
                if (cs2_read(&c->t, ent + CS2_NV_M_FIRECOUNT, fb, 8)) {
                    RtlCopyMemory(&fc, fb, 4);
                    RtlCopyMemory(&itype, fb + 4, 4);
                    if (fc >= 1 && fc <= 64 && itype >= 0 && itype <= 2) {
                        u8 pts[64 * 12];
                        u8 burn[64];
                        u8 tailb[12];
                        int k, cnt = 0;
                        f32 sx = 0.0f, sy = 0.0f, sz = 0.0f;
                        if (cs2_read(&c->t, ent + CS2_NV_M_FIREPOSITIONS, pts, sizeof(pts)) &&
                            cs2_read(&c->t, ent + CS2_NV_M_BFIREISBURNING, burn, sizeof(burn))) {
                            for (k = 0; k < fc && k < 64; k++) {
                                f32* p = (f32*)(pts + k * 12);
                                if (!burn[k]) continue;
                                if (!item_vec_ok(p) || item_is_zero(p)) continue;
                                sx += p[0]; sy += p[1]; sz += p[2]; cnt++;
                            }
                            if (cnt > 0) {
                                fire_c[0] = sx / (f32)cnt;
                                fire_c[1] = sy / (f32)cnt;
                                fire_c[2] = sz / (f32)cnt;
                                for (k = 0; k < fc && k < 64; k++) {
                                    f32* p = (f32*)(pts + k * 12);
                                    f32 dx, dy, d2;
                                    if (!burn[k]) continue;
                                    if (!item_vec_ok(p) || item_is_zero(p)) continue;
                                    dx = p[0] - fire_c[0]; dy = p[1] - fire_c[1];
                                    d2 = dx * dx + dy * dy;
                                    if (d2 > fire_r) fire_r = d2;
                                }
                                /* 平方 -> 半径；上下夹一下，避免异常点把圈撑爆 */
                                {
                                    f32 rr = fire_r, x = rr * 0.5f;
                                    int it;
                                    if (rr > 0.0f) { for (it = 0; it < 16 && x > 0.0f; it++) x = 0.5f * (x + rr / x); }
                                    fire_r = x;
                                }
                                if (fire_r < 40.0f) fire_r = 40.0f;
                                if (fire_r > 600.0f) fire_r = 600.0f;
                                if (cs2_read(&c->t, ent + CS2_NV_M_NFIRELIFETIME, tailb, 4)) {
                                    RtlCopyMemory(&lf, tailb, 4);
                                }
                                if (!(lf > 0.0f && lf < 120.0f)) lf = 7.0f;
                                fire_pts = (u32)cnt;
                                is_fire = 1;
                            }
                        }
                    }
                }
            }

            if (is_fire) {
                rec = tail2 + CS2_OFF_G_RECS + n * CS2CORE_GREN_SIZE;
                RtlZeroMemory(rec, CS2CORE_GREN_SIZE);
                wr_f32(rec, CS2_OFF_R_POS + 0, fire_c[0]);
                wr_f32(rec, CS2_OFF_R_POS + 4, fire_c[1]);
                wr_f32(rec, CS2_OFF_R_POS + 8, fire_c[2]);
                wr_f32(rec, CS2_OFF_R_RADIUS, fire_r);
                wr_f32(rec, CS2_OFF_R_DETONATE, lf);          /* kind=fire: 这里放"总时长(秒)" */
                wr_f32(rec, CS2_OFF_R_SPAWN, 0.0f);
                rec[CS2_OFF_R_KIND]  = CS2_GREN_KIND_FIRE;
                rec[CS2_OFF_R_FLAGS] = CS2_GREN_F_LIVE;
                *(u32*)(rec + CS2_OFF_R_EXTRA) = fire_pts;
                n++;
                if (n >= CS2CORE_GREN_MAX) break;
                continue;
            }

            if (!is_proj) continue;

            if (!gren_pos(c, ent, pos)) {
                RtlCopyMemory(pos, ipos, 12);
            }
            if (!item_vec_ok(pos) || item_is_zero(pos)) continue;

            rec = tail2 + CS2_OFF_G_RECS + n * CS2CORE_GREN_SIZE;
            RtlZeroMemory(rec, CS2CORE_GREN_SIZE);
            wr_f32(rec, CS2_OFF_R_POS + 0, pos[0]);
            wr_f32(rec, CS2_OFF_R_POS + 4, pos[1]);
            wr_f32(rec, CS2_OFF_R_POS + 8, pos[2]);
            wr_f32(rec, CS2_OFF_R_VEL + 0, item_vec_ok(ivel) ? ivel[0] : 0.0f);
            wr_f32(rec, CS2_OFF_R_VEL + 4, item_vec_ok(ivel) ? ivel[1] : 0.0f);
            wr_f32(rec, CS2_OFF_R_VEL + 8, item_vec_ok(ivel) ? ivel[2] : 0.0f);
            wr_f32(rec, CS2_OFF_R_INITPOS + 0, item_vec_ok(ipos) ? ipos[0] : pos[0]);
            wr_f32(rec, CS2_OFF_R_INITPOS + 4, item_vec_ok(ipos) ? ipos[1] : pos[1]);
            wr_f32(rec, CS2_OFF_R_INITPOS + 8, item_vec_ok(ipos) ? ipos[2] : pos[2]);
            wr_f32(rec, CS2_OFF_R_RADIUS, dmg_r);
            wr_f32(rec, CS2_OFF_R_DETONATE, det);
            wr_f32(rec, CS2_OFF_R_SPAWN, spawn);
            wr_u16(rec, CS2_OFF_R_ID, 0);
            rec[CS2_OFF_R_KIND] = CS2_GREN_KIND_PROJ;
            rec[CS2_OFF_R_FLAGS] = (u8)((live ? CS2_GREN_F_LIVE : 0u) |
                                        (bounces > 0 ? CS2_GREN_F_BOUNCED : 0u) |
                                        (smoke ? CS2_GREN_F_SMOKE : 0u));
            *(u32*)(rec + CS2_OFF_R_EXTRA) = (u32)(bounces < 0 ? 0 : bounces);
            n++;
        }
    }

    return n;
}

static void finish_grenade_tail(cs2_ctx* c, u8* frame)
{
    u8* tail2 = frame + CS2_FRAME_SIZE_V2;
    u32 n;

    RtlZeroMemory(tail2, CS2_GRND_TAIL_SIZE);
    if ((g_tick & 7u) == 0u) {
        g_grnd_count = scan_grenades(c, c->client.base, tail2);
    } else {
        /* 复用上一次扫描的结果：帧尾是"每帧重建"的，所以要把记录搬回来 */
        RtlCopyMemory(tail2 + CS2_OFF_G_RECS, g_grnd_saved + CS2_OFF_G_RECS,
                      CS2CORE_GREN_MAX * CS2CORE_GREN_SIZE);
        n = g_grnd_count;
        wr_u32(tail2, CS2_OFF_G_MAGIC,   CS2CORE_GRND_MAGIC);
        wr_u32(tail2, CS2_OFF_G_VERSION, CS2CORE_GRND_VERSION);
        wr_u32(tail2, CS2_OFF_G_COUNT,   n);
        wr_u32(tail2, CS2_OFF_G_FLAGS,   n ? 1u : 0u);
        wr_u32(tail2, CS2_OFF_G_CHECKSUM, fnv1a(tail2, CS2_OFF_G_CHECKSUM));
        return;
    }

    wr_u32(tail2, CS2_OFF_G_MAGIC,   CS2CORE_GRND_MAGIC);
    wr_u32(tail2, CS2_OFF_G_VERSION, CS2CORE_GRND_VERSION);
    wr_u32(tail2, CS2_OFF_G_COUNT,   g_grnd_count);
    wr_u32(tail2, CS2_OFF_G_FLAGS,   g_grnd_count ? 1u : 0u);
    wr_u32(tail2, CS2_OFF_G_CHECKSUM, fnv1a(tail2, CS2_OFF_G_CHECKSUM));
    RtlCopyMemory(g_grnd_saved, tail2, CS2_GRND_TAIL_SIZE);
}

static void finish_tail(cs2_ctx* c, u8* frame, u32 aabb_mask)
{
    u8* tail = frame + CS2_FRAME_SIZE;

    if ((g_tick & 7u) == 0u) {
        RtlZeroMemory(tail + CS2_OFF_T_ITEMS, CS2CORE_ITEM_MAX * CS2CORE_ITEM_SIZE);
        g_tail_items = scan_items(c, c->client.base, tail);
    }

    wr_u32(tail, CS2_OFF_T_MAGIC,     CS2CORE_TAIL_MAGIC);
    wr_u32(tail, CS2_OFF_T_VERSION,   CS2CORE_TAIL_VERSION);
    wr_u32(tail, CS2_OFF_T_AABBMASK,  aabb_mask);
    wr_u32(tail, CS2_OFF_T_ITEMCNT,   g_tail_items);
    wr_u32(tail, CS2_OFF_T_TAILCHECK, fnv1a(tail, CS2_OFF_T_TAILCHECK));

    finish_grenade_tail(c, frame);
}

/* ---------------------------------------------------------- 视矩阵结构校验
 *
 * dwViewMatrix 是行主序 transpose(P*V):
 *   row0 = right   * (1/tanH)   带平移 m[3]  = -(r·eye)/tanH
 *   row1 = up      * (1/tanV)   带平移 m[7]  = -(u·eye)/tanV
 *   row2 = forward * 1          带平移 m[11] = -(f·eye)
 *   row3 = forward * 1          带平移 m[15]（= w 分量）
 *
 * 只要"三行长度合理 + 前向行是单位向量 + 不是恒等/单位阵"就认定可信。
 * 之前踩过的坑：运行期扫描偶尔命中 .data 里的单位阵，那种矩阵投出来
 * 所有目标都在屏幕中心 —— 表现为"框全部叠在准星上"。
 * ------------------------------------------------------------------------ */
static f32 g_vm_raw[16];
static int g_vm_ok;
/* Last matrix that passed the sanity check. Scope transitions update
 * dwViewMatrix late (or publish a temp value), and a single rejected frame used
 * to blank the whole projection for the client. Keep the previous good matrix
 * and reuse it instead. */
static f32 g_vm_last[16];
static int g_vm_last_ok;

static int vm_finite(f32 v)
{
    return (v == v) && (v > -1.0e9f) && (v < 1.0e9f);
}

static int view_matrix_sane(const f32* m)
{
    f32 rl2, ul2, fl2;
    int i;

    for (i = 0; i < 16; i++) { if (!vm_finite(m[i])) return 0; }

    rl2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
    ul2 = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
    fl2 = m[12] * m[12] + m[13] * m[13] + m[14] * m[14];   /* 第 4 行是 w 行 = 相机前向 */

    /* 1/tanH^2：scope 会把 FOV 压到 15° 甚至 5°，rl2 随之涨到几十到几千。
     * 上限必须覆盖 FOV 3°（1/tan(1.5°)^2 ≈ 1457），否则开镜瞬间整帧矩阵被判无效，
     * 显示端拿不到投影 -> 所有方框消失。 */
    if (rl2 < 0.0005f || rl2 > 8192.0f) return 0;
    if (ul2 < 0.0005f || ul2 > 8192.0f) return 0;
    /* w 行的模长在 scope 下也可能被缩放，放宽到 0.05..64（仍能杀掉全 0 矩阵） */
    if (fl2 < 0.05f || fl2 > 64.0f) return 0;

    /* 平移全 0 = 没乘过 V 的"假矩阵"（常见于命中 .data 里的单位阵） */
    if (m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 0.0f) return 0;

    return 1;
}

static int sample_frame_impl(cs2_ctx* c, u8* frame)
{
    /* 游戏更新/重启会把进程换掉：先确认手里的 PEPROCESS 还是同一个、还活着，
     * 否则整帧直接放弃（调用方会 close + 下一 tick 重新 open），绝不带着旧句柄去读。 */
    if (!cs2_target_alive(&c->t)) { return CS2_FAIL; }

    u32 screen_w = 0, screen_h = 0;
    i32 slot;
    u64 local_ctrl;
    u32 player_count = 0;
    i32 i;
    u64 engine_base = c->engine.base;
    u64 client_base = c->client.base;

    u8* tail = frame + CS2_FRAME_SIZE;
    u32 aabb_mask = 0;

    ent_cache_begin();
    g_tick++;

    /* 瑙嗗浘鐭╅樀: 鍏堢敤瑙ｆ瀽鍑烘潵鐨勯敋鐐? 璇诲埌鐨勪笉鍍忕煩闃靛氨閫€鍥炵瀛愰敋鐐瑰啀璇讳竴娆?
     * (杩愯鏈熸壂鎻忓伓灏斾細鍛戒腑鍒殑 .data 閲岀殑鍗曚綅�? 閭ｆ槸搴熸暟�? */
    /* 原样转发 dwViewMatrix。注意：这里只读进局部缓冲 + 做结构校验，
     * 真正写帧要等到下面 RtlZeroMemory 之后 —— 旧代码在这个位置写，
     * 紧接着的 RtlZeroMemory(frame, CS2_FRAME_SIZE) 又把它清零了，
     * 于是组件永远拿到全 0 矩阵、只能自建投影（FOV 约定差 1.33 倍 = 偏框）。
     * 校验不过就保持全 0，让组件退回"角度自建矩阵"，绝不把垃圾矩阵写出去。 */
    {
        RtlZeroMemory(g_vm_raw, sizeof(g_vm_raw));
        g_vm_ok = 0;
        if (cs2_read(&c->t, client_base + (u64)CS2_DW_VIEWMATRIX, g_vm_raw, sizeof(g_vm_raw)) &&
            view_matrix_sane(g_vm_raw)) {
            g_vm_ok = 1;
            RtlCopyMemory(g_vm_last, g_vm_raw, sizeof(g_vm_last));
            g_vm_last_ok = 1;
        } else if (g_vm_last_ok) {
            /* Scope transition: reuse the last good matrix so boxes stay on
             * screen (slightly stale for one tick) instead of vanishing. */
            RtlCopyMemory(g_vm_raw, g_vm_last, sizeof(g_vm_last));
            g_vm_ok = 1;
        }
    }
    /* 娓告垙鍐呴儴娓叉煋鍒嗚鲸�?+ 妗岄潰瑙嗗彛 (鏈夐粦杈?绐楀彛鍖栨椂 Stage-B 鐢ㄥ畠鍋氭槧�? */
    if (engine_base) {
        cs2_read(&c->t, engine_base + (u64)c->off.dwWindowWidth, &screen_w, 4);
        cs2_read(&c->t, engine_base + (u64)c->off.dwWindowHeight, &screen_h, 4);
    }

    local_ctrl = cs2_local_controller(&c->t, &c->off, client_base, &slot);
    if (local_ctrl == 0) return CS2_FAIL;

    /* 本地玩家: 队伍 / 原点 (给距离和队友判定用) */
    c->local_slot = slot;
    c->local_fov = 0.0f;      /* 每帧重置：开镜结束后不能留着上一帧的 FOV */
    {
        u32 h = 0;
        cs2_read(&c->t, local_ctrl + CS2_NV_M_ITEAMNUM, &c->local_team, 1);
        if (cs2_read(&c->t, local_ctrl + CS2_NV_M_HPLAYERPAWN, &h, 4) &&
            h != 0 && h != 0xFFFFFFFFu) {
            u64 lp = cs2_entity_at(&c->t, &c->off, client_base, (i32)(h & CS2_ENT_HANDLE_MASK));
            if (lp) {
                u64 sn = 0;
                if (cs2_read(&c->t, lp + CS2_NV_M_PGAMESCENENODE, &sn, 8) && sn > 0x10000ull)
                    cs2_read(&c->t, sn + CS2_NV_M_VECABSORIGIN, c->local_org, 12);
                /* 当前视野：pawn + m_pCameraServices(4904) -> + m_iFOV(664)。
                 * 开镜时游戏只改这个值，不改 dwViewMatrix，所以要单独读出来，
                 * 由组件按 tan(腰射/2)/tan(当前/2) 缩放投影。 */
                {
                    u64 svc = 0;
                    i32 fov_i = 0;
                    if (cs2_read(&c->t, lp + 4904ull, &svc, 8) && svc > 0x10000ull &&
                        cs2_read(&c->t, svc + 664ull, &fov_i, 4) &&
                        fov_i > 5 && fov_i < 130)
                    {
                        c->local_fov = (f32)fov_i;
                    }
                }
            }
        }
    }

    RtlZeroMemory(frame, CS2_FRAME_SIZE);
    /* v6 tail: AABB 区每帧重建（玩家在动）；物品区沿用上一次扫描结果，所以不清 */
    RtlZeroMemory(tail + CS2_OFF_T_AABB, CS2_OFF_T_ITEMS - CS2_OFF_T_AABB);

    /* seqlock：先置奇数（读端看到奇数就放弃这一帧），全部写完再置偶数。
     * 旧实现只有结尾那一次写入、而且 c->seq 从来不递增，
     * 读端的"撕裂检测"（seq 前后不一致）等于没开。 */
    c->seq += 2u;
    wr_u32(frame, CS2_OFF_F_SEQ, c->seq | 1u);

    /* 校验通过的真实视矩阵放在这里写（放在所有清零之后） */
    if (g_vm_ok) {
        for (i = 0; i < 16; i++) { wr_f32(frame, CS2_OFF_F_VIEWMAT + (u32)i * 4, g_vm_raw[i]); }
    }

    wr_u32(frame, CS2_OFF_F_MAGIC,   CS2CORE_MAGIC);
    wr_u32(frame, CS2_OFF_F_VERSION, CS2CORE_VERSION);
    wr_u32(frame, CS2_OFF_F_FLAGS,   CS2_FS_IN_GAME);
    wr_u32(frame, CS2_OFF_F_SCREENW, (i32)(screen_w ? screen_w : 1920));
    wr_u32(frame, CS2_OFF_F_SCREENH, (i32)(screen_h ? screen_h : 1080));
    wr_i32(frame, CS2_OFF_F_LOCALSLOT, (i32)c->local_slot);
    wr_f32(frame, CS2_OFF_F_FOV, c->local_fov > 1.0f ? c->local_fov : 0.0f);
    wr_i32(frame, CS2_OFF_F_VIEWPORTX, 0);
    wr_i32(frame, CS2_OFF_F_VIEWPORTY, 0);
    wr_i32(frame, CS2_OFF_F_VIEWPORTW, (i32)(screen_w));
    wr_i32(frame, CS2_OFF_F_VIEWPORTH, (i32)(screen_h));
    wr_u64(frame, CS2_OFF_F_SAMPLETICK, (u64)(KeQueryInterruptTime() / 10000ull));
    /* 纯采集：VIEWMAT 保持全 0（RtlZeroMemory 已清零），组件走自算投影。 */

    for (i = 1; i <= CS2CORE_MAX_PLAYERS; i++) {
        u64 ctrl = ent_at(&c->t, &c->off, client_base, i);
        u64 pawn = 0;
        u32 h = 0;
        u8* p;
        u32 off_bones;
        u8  team = 0, life = 0, dormant = 0, spotted = 0;
        i32 health = 0;
        f32 org[3], eye[3], ang[2], vel[3];
        u16 weapon = 0, ammo = 0;
        u8  armor = 0, gear = 0;
        u32 flags = 0;
        u32 bone_valid = 0;
        u64 scene = 0;
        int bone_i;
        /* 鎺у埗鍣ㄤ竴娆¤�?0x3E7..0x946: 闃熶�?鍚嶅�?鏈湴鏍囪/pawn 鍙ユ�?鍏滃簳琛€閲?*/
        u8  cb[0x560];
        int cb_ok = 0;

        if (ctrl == 0) continue;

        /* slot 是指纹不是身份证：ctrl 一变就说明这格换人了。
         * 不清缓存的话，新玩家会先顶着上一个人的骨架/武器跑几帧。 */
        if (g_sc[i - 1].name_ctrl != ctrl) {
            RtlZeroMemory(&g_sc[i - 1], sizeof(g_sc[i - 1]));
        }

        p = frame + CS2_OFF_F_PLAYERS + (u32)(i - 1) * CS2_PLAYER_SIZE;
        wr_u32(p, CS2_OFF_P_SLOT, (u32)(i - 1));

        RtlZeroMemory(cb, sizeof(cb));
        if (!cs2_read(&c->t, ctrl + CS2_NV_M_ITEAMNUM, cb, sizeof(cb))) continue;
        cb_ok = 1;
        team = cb[0];
        RtlCopyMemory(&h, cb + (CS2_NV_M_HPLAYERPAWN - CS2_NV_M_ITEAMNUM), 4);

        wr_u32(p, CS2_OFF_P_TEAM, team);

        if (h != 0 && h != 0xFFFFFFFFu)
            pawn = ent_at(&c->t, &c->off, client_base, (i32)(h & CS2_ENT_HANDLE_MASK));

        if (pawn) {
            /* pawn 涓€娆¤ 0x330..0x6EF: 鍦烘櫙鑺傜偣/琛€�?鏈€澶ц/姝绘�?鏃楁�?閫熷�?*/
            u8 pb[0x3C0];
            RtlZeroMemory(pb, sizeof(pb));
            if (cs2_read(&c->t, pawn + CS2_NV_M_PGAMESCENENODE, pb, sizeof(pb))) {
                RtlCopyMemory(&scene, pb + 0, 8);
                RtlCopyMemory(&health, pb + (CS2_NV_M_IHEALTH - CS2_NV_M_PGAMESCENENODE), 4);
                life = pb[CS2_NV_M_LIFESTATE - CS2_NV_M_PGAMESCENENODE];
                RtlCopyMemory(vel, pb + (CS2_NV_M_VECVELOCITY - CS2_NV_M_PGAMESCENENODE), 12);
            } else {
                scene = 0;
            }
            /* 瑁呭灏忔 0x1E78..0x1ED7: 璧拌�?琚湅鍒?寮€闀?鎷嗗�?鎶ょ�?*/
            {
                u8 gb[0x60];
                RtlZeroMemory(gb, sizeof(gb));
                if (cs2_read(&c->t, pawn + CS2_NV_M_BISWALKING, gb, sizeof(gb))) {
                    spotted = gb[(CS2_NV_M_ENTITYSPOTTEDSTATE + CS2_NV_ES_M_BSPOTTED) - CS2_NV_M_BISWALKING];
                    {
                        i32 av = 0;
                        RtlCopyMemory(&av, gb + (CS2_NV_M_ARMORVALUE - CS2_NV_M_BISWALKING), 4);
                        if (av < 0) av = 0;
                        if (av > 100) av = 100;
                        armor = (u8)av;
                        if (armor > 0) gear |= CS2_GEAR_ARMORED;
                    }
                    if (gb[CS2_NV_M_BISSCOPED - CS2_NV_M_BISWALKING]) gear |= CS2_GEAR_ZOOMED;
                    if (gb[CS2_NV_M_BISDEFUSING - CS2_NV_M_BISWALKING]) gear |= CS2_GEAR_DEFUSING;
                }
            }
            if (scene > 0x10000ull) {
                if (cs2_read(&c->t, scene + CS2_NV_M_VECABSORIGIN, org, 12)) {
                    u8 d = 0;
                    if (cs2_read(&c->t, scene + CS2_NV_M_BDORMANT, &d, 1) && d) dormant = 1;
                } else {
                    scene = 0;
                }
            }
        }

        if (scene == 0) {
            /* 鎷夸笉鍒?pawn/鍦烘�? 鐢ㄦ帶鍒跺櫒涓婄殑琛€閲忓厹�? �?dormant 璁╂樉绀虹鍙烦杩囥€?
             * 琛€閲忓凡缁忓湪涓婇潰閭ｆ 0x560 鍧楄閲屼簡, 涓嶅啀棰濆璇汇€?*/
            if (cb_ok) { RtlCopyMemory(&health, cb + (CS2_NV_M_IPAWNHEALTH - CS2_NV_M_ITEAMNUM), 4); }
            dormant = 1;
            /* 之前缓存的是"上一次能读到时的绝对坐标"，人已经走了/进雾了，
             * 继续发就是屏幕上钉一个不动的骨架。 */
            g_sc[i - 1].bone_valid = 0;
            RtlZeroMemory(org, sizeof(org));
            RtlZeroMemory(vel, sizeof(vel));
        }

        /* 鐪肩潧浣嶇疆 = 鍘熺�?+ m_vecViewOffset */
        if (pawn) {
            f32 vo[3];
            RtlZeroMemory(vo, sizeof(vo));
            if (cs2_read(&c->t, pawn + CS2_NV_M_VECVIEWOFFSET, vo, 12) == CS2_OK) {
                eye[0] = org[0] + vo[0];
                eye[1] = org[1] + vo[1];
                eye[2] = org[2] + vo[2];
            } else {
                eye[0] = org[0]; eye[1] = org[1]; eye[2] = org[2] + 64.0f;
            }
            RtlZeroMemory(ang, sizeof(ang));
            cs2_read(&c->t, pawn + CS2_NV_M_ANGEYEANGLES, ang, 8);
            // v1.4.8: weapon/armor detail every 8th tick per slot, cached in between
            if (((g_tick + (u32)i) & 7u) == 0u) {
                weapon = 0; ammo = 0; gear &= (u8)~(CS2_GEAR_ARMORED | CS2_GEAR_ZOOMED | CS2_GEAR_DEFUSING);
                read_weapon_detail(&c->t, &c->off, client_base, pawn, &weapon, &ammo, &gear);
                g_sc[i - 1].weapon = weapon; g_sc[i - 1].ammo = ammo; g_sc[i - 1].gear = gear;
            } else {
                weapon = g_sc[i - 1].weapon; ammo = g_sc[i - 1].ammo;
                gear |= g_sc[i - 1].gear;
            }
        } else {
            eye[0] = org[0]; eye[1] = org[1]; eye[2] = org[2] + 64.0f;
            RtlZeroMemory(ang, sizeof(ang));
        }

        /* 鏃楁�?*/
        if (life == 0 && health > 0) flags |= CS2_PF_ALIVE;
        if (i == c->local_slot)      flags |= CS2_PF_LOCAL;
        if (dormant)                 flags |= CS2_PF_DORMANT;
        if (team == c->local_team && team != 0) flags |= CS2_PF_TEAMMATE;
        if (spotted)                 flags |= CS2_PF_SPOTTED;
        if (gear & CS2_GEAR_DEFUSING) flags |= CS2_PF_DEFUSING;

        /* 楠ㄩ�?*/
        off_bones = CS2_OFF_P_BONES;
        if (pawn && scene > 0x10000ull && ((g_tick + (u32)i) & 3u) == 0u) {
            u64 bone_array = 0;
            if (cs2_read(&c->t, scene + CS2_NV_M_MODELSTATE + CS2_BONE_ARRAY_FROM_MODELSTATE,
                         &bone_array, 8) == CS2_OK && bone_array > 0x10000ull) {
                u8 native_idx[CS2CORE_MAX_BONES];
                native_idx[0]  = CS2_BONE_IDX_HEAD;
                native_idx[1]  = CS2_BONE_IDX_NECK;
                native_idx[2]  = CS2_BONE_IDX_CHEST;
                native_idx[3]  = CS2_BONE_IDX_STOMACH;
                native_idx[4]  = CS2_BONE_IDX_PELVIS;
                native_idx[5]  = CS2_BONE_IDX_SHOLDER_L;
                native_idx[6]  = CS2_BONE_IDX_ELBOW_L;
                native_idx[7]  = CS2_BONE_IDX_HAND_L;
                native_idx[8]  = CS2_BONE_IDX_SHOLDER_R;
                native_idx[9]  = CS2_BONE_IDX_ELBOW_R;
                native_idx[10] = CS2_BONE_IDX_HAND_R;
                native_idx[11] = c->idx_hip_l;
                native_idx[12] = c->idx_knee_l;
                native_idx[13] = c->idx_foot_l;
                native_idx[14] = c->idx_hip_r;
                native_idx[15] = c->idx_knee_r;
                native_idx[16] = c->idx_foot_r;
                if (!c->bone_calib) { (void)bone_calibrate(c, bone_array, org); }
                if (c->bone_calib && !c->bone_map) { bone_map_legs(c, bone_array); }
                native_idx[11] = c->idx_hip_l;
                native_idx[14] = c->idx_hip_r;
                for (bone_i = 0; bone_i < CS2CORE_MAX_BONES; bone_i++) {
                    f32 bp[3];
                    if (!c->bone_calib) { break; }      /* 标定失败：宁可不写，也不写四元数 */
                    if (cs2_read(&c->t,
                                 bone_array + (u64)c->bone_stride * (u64)native_idx[bone_i] +
                                     (u64)c->bone_pos,
                                 bp, 12) == CS2_OK) {
                        g_sc[i - 1].bone[bone_i][0] = bp[0];
                        g_sc[i - 1].bone[bone_i][1] = bp[1];
                        g_sc[i - 1].bone[bone_i][2] = bp[2];
                        bone_valid |= (1u << bone_i);
                    }
                }
            }
        }

        /* 鍐欑帺瀹惰褰?*/
        // v1.4.8: bones are refreshed every 4th tick per slot; emit the cached
        // set into the frame every tick so the renderer never sees a bare skeleton.
        if (bone_valid) { g_sc[i - 1].bone_valid = bone_valid; }
        if (g_sc[i - 1].bone_valid) {
            for (bone_i = 0; bone_i < CS2CORE_MAX_BONES; bone_i++) {
                if ((g_sc[i - 1].bone_valid & (1u << bone_i)) == 0u) { continue; }
                f32* dstc = (f32*)(p + off_bones + (u32)bone_i * 12);
                dstc[0] = g_sc[i - 1].bone[bone_i][0];
                dstc[1] = g_sc[i - 1].bone[bone_i][1];
                dstc[2] = g_sc[i - 1].bone[bone_i][2];
            }
            bone_valid = g_sc[i - 1].bone_valid;
        }

        /* v6: world AABB from CCollisionProperty. m_vecMins/Maxs are model
         * space with z already relative to the feet; the hull is square in xy,
         * so the larger half-extent on both axes is an exact box (no sin/cos
         * needed in the kernel) and can never be offset from the player. */
        if (pawn && scene > 0x10000ull && life == 0 && health > 0) {
            f32 mn[3], mx[3];
            if (cs2_read(&c->t, pawn + CS2_NV_M_COLLISION + CS2_NV_M_VECMINS, mn, 12) &&
                cs2_read(&c->t, pawn + CS2_NV_M_COLLISION + CS2_NV_M_VECMAXS, mx, 12) &&
                item_vec_ok(mn) && item_vec_ok(mx) &&
                mx[0] > mn[0] && mx[1] > mn[1] && mx[2] > mn[2]) {
                f32 hx = (mx[0] - mn[0]) * 0.5f;
                f32 hy = (mx[1] - mn[1]) * 0.5f;
                f32 cx = (mx[0] + mn[0]) * 0.5f;
                f32 cy = (mx[1] + mn[1]) * 0.5f;
                f32 r  = hx > hy ? hx : hy;
                f32 height = mx[2] - mn[2];
                if (r > 2.0f && r < 60.0f && height > 10.0f && height < 200.0f) {
                    u8* ab = tail + CS2_OFF_T_AABB + (u32)(i - 1) * CS2_TAIL_AABB_STRIDE;
                    wr_f32(ab, 0,  org[0] + cx - r);
                    wr_f32(ab, 4,  org[1] + cy - r);
                    wr_f32(ab, 8,  org[2] + mn[2]);
                    wr_f32(ab, 12, org[0] + cx + r);
                    wr_f32(ab, 16, org[1] + cy + r);
                    wr_f32(ab, 20, org[2] + mx[2]);
                    aabb_mask |= (1u << (u32)(i - 1));
                }
            }
        }

        wr_i32(p, CS2_OFF_P_HEALTH, health);
        wr_i32(p, CS2_OFF_P_LIFESTATE, life);
        wr_u32(p, CS2_OFF_P_FLAGS, flags);
        wr_u32(p, CS2_OFF_P_BONEVALID, bone_valid);
        // v1.4.8: name only changes when the slot is reused -> cache by controller
        if (g_sc[i - 1].name_ctrl != ctrl) {
            read_name(&c->t, ctrl, g_sc[i - 1].name, CS2CORE_NAME_LEN);
            g_sc[i - 1].name_ctrl = ctrl;
        }
        if (g_sc[i - 1].name[0]) {
            RtlCopyMemory(p + CS2_OFF_P_NAME, g_sc[i - 1].name, CS2CORE_NAME_LEN);
        }
        wr_vec3(p, CS2_OFF_P_ORIGIN, org);
        wr_vec3(p, CS2_OFF_P_EYEPOS, eye);
        wr_f32(p, CS2_OFF_P_EYEANGLES, ang[0]);
        wr_f32(p, CS2_OFF_P_EYEANGLES + 4, ang[1]);
        wr_vec3(p, CS2_OFF_P_VELOCITY, vel);
        {
            f32 dx = org[0] - c->local_org[0];
            f32 dy = org[1] - c->local_org[1];
            f32 dz = org[2] - c->local_org[2];
            f32 d = dx * dx + dy * dy + dz * dz;
            /* 鏁存暟寮€鏂圭殑绠€�? �?f32 杩戜技鍗冲�? 鏄剧ず绔彧�?"xx m" 鍙栨�?*/
            if (d > 0.0f) {
                f32 r = d, x = d * 0.5f;
                int it;
                for (it = 0; it < 16 && x > 0.0f; it++) { x = 0.5f * (x + r / x); }
                wr_f32(p, CS2_OFF_P_DISTANCE, x * CS2_UNITS_TO_M);
            } else {
                wr_f32(p, CS2_OFF_P_DISTANCE, 0.0f);
            }
        }
        wr_u16(p, CS2_OFF_P_WEAPON, weapon);
        wr_u16(p, CS2_OFF_P_AMMO, ammo);
        wr_u8(p, CS2_OFF_P_ARMOR, armor);
        wr_u8(p, CS2_OFF_P_GEAR, gear);
        player_count++;
    }

    wr_u32(frame, CS2_OFF_F_PLAYERCNT, player_count);
    finish_tail(c, frame, aabb_mask);
    {
        static const char mapname[] = "";
        u32 k;
        for (k = 0; k < CS2CORE_MAP_LEN && mapname[k]; k++)
            *(frame + CS2_OFF_F_MAP + k) = (u8)mapname[k];
    }
    /* checksum 瑕嗙�?players 鍖洪�?(�?C# �?Fnv1a 涓€�? */
    /* ��"��������۾�λ�� + ȫ�� dwViewAngles"�Լ��� view-projection��
     * ��Ϸ�Լ��� dwViewMatrix ��ĳЩ״̬�����˻��ģ������˱�Ͷ����Ļ���ģ��� */
    {
        u32 pi2;
        for (pi2 = 0; pi2 < player_count; pi2++)
        {
            const u8* pp = frame + CS2_OFF_F_PLAYERS + pi2 * CS2_PLAYER_SIZE;
            u32 fl = *(const u32*)(pp + CS2_OFF_P_FLAGS);
            if (fl & CS2_PF_LOCAL)
            {
                const f32* eye2 = (const f32*)(pp + CS2_OFF_P_EYEPOS);
                f32 ang2[2] = { 0.0f, 0.0f };
                if (!cs2_read(&c->t, client_base + (u64)CS2_DW_VIEWANGLES, ang2, sizeof(ang2)))
                {
                    const f32* pa = (const f32*)(pp + CS2_OFF_P_EYEANGLES);
                    ang2[0] = pa[0]; ang2[1] = pa[1];
                }
                /* 眼睛位置兜底：缺失（观战/刚进图/记录未填）就用原点+64，
                 * 否则矩阵会把所有人投到屏幕外。 */
                {
                    f32* pe2 = (f32*)(pp + CS2_OFF_P_EYEPOS);
                    if (pe2[0] == 0.0f && pe2[1] == 0.0f && pe2[2] == 0.0f)
                    {
                        const f32* po2 = (const f32*)(pp + CS2_OFF_P_ORIGIN);
                        pe2[0] = po2[0]; pe2[1] = po2[1]; pe2[2] = po2[2] + 64.0f;
                        eye2 = (const f32*)(pp + CS2_OFF_P_EYEPOS);
                    }
                }
                /* 角度：本机 pawn 的 m_angEyeAngles 对自己并不刷新（那是同步别人的），
                 * dwViewAngles 才是"我现在看哪儿"的权威值 —— 无条件覆盖，
                 * 不再"只在读到 0 时才用"（旧写法一旦 m_angEyeAngles 是上一局的
                 * 残留值，矩阵就是错的，雷达朝向也跟着反）。 */
                {
                    f32* pa3 = (f32*)(pp + CS2_OFF_P_EYEANGLES);
                    if (vm_finite(ang2[0]) && vm_finite(ang2[1]) &&
                        (ang2[0] != 0.0f || ang2[1] != 0.0f)) {
                        pa3[0] = ang2[0];
                        pa3[1] = ang2[1];
                    }
                }
                /* 驱动不再自算矩阵：原样转发游戏自己的 dwViewMatrix（参考新AAC 的做法）。
                /* 纯采集：矩阵与派生量都不写，只写原始量。 */
                break;
            }
        }
    }

    wr_u32(frame, CS2_OFF_F_CHECKSUM, 0);
    wr_u32(frame, CS2_OFF_F_CHECKSUM,
           fnv1a(frame + CS2_OFF_F_PLAYERS, CS2CORE_MAX_PLAYERS * CS2_PLAYER_SIZE));
    wr_u32(frame, CS2_OFF_F_SEQ, c->seq);   /* 偶数 = 稳定可读 */
    return CS2_OK;
}

/* -------------------------------------------------------------- 閲囨牱涓讳綋 */
/* v1.4 闅愯斀鎬ц�?(涓庡師瀹炵幇鐨勪笁鐐瑰樊寮傞兘瀵瑰簲涓€鏉℃娴嬮�?:
 *
 *  1. 涓嶇�?PsCreateSystemThread銆傜郴缁熺嚎绋嬬�?ETHREAD->StartAddress 鎸囧�?
 *     "涓嶅睘浜庝换浣曞凡娉ㄥ唽妯″潡"鐨勫湴鍧�? �?AC 鏋氫�?system 杩涚▼绾跨▼鏃舵渶鐩存帴鐨勪竴鏉?
 *     鍒ゆ�? 涔嬪墠闈犳敼�?ETHREAD 鏉ヤ吉瑁? �?鏀?ETHREAD"鏈韩鍙堢暀涓嬭鏀瑰啓鐨勭棔杩广€?
 *     鐜板湪鏁存潯閲囨牱璺戝湪 ExQueueWorkItem �?nt! 绯荤�?worker �?鈥斺�?鍚姩鍦板潃澶╃�?
 *     钀藉湪 ntoskrnl �? 涓嶉渶瑕佷换浣曚吉瑁? 涔熸病鏈夎鏀瑰啓鐨勫瓧娈点€?
 *
 *  2. 涓嶅�?涓嶅啓浠讳綍璇婃�?section銆傛棫鐗堟瘡 8 �?tick 寰€ _sm2_dbg 鍐欎竴娆?DIAG,
 *     绛変簬鍦?\Sessions\<n>\BaseNamedObjects 閲屽父椹讳竴涓睘浜庢湭鐭ラ┍鍔ㄧ殑鍚嶅�?
 *     PAC / 5E 鐨勫璞℃灇涓句竴鎶撳氨鏄畠銆傜幇鍦ㄨ繍琛屾湡鍐呮牳閲?*闆跺懡鍚嶅�?*�?
 *
 *  3. 杩涚▼鏋氫妇鎸囨暟閫€閬?(1s �?2s �?4s �?8s)銆俢tx_open �?
 *     ZwQuerySystemInformation(SystemProcessInformation), 杩欐�?AC �?hook �?
 *     鍏ュ�? 绌洪棽鏈?姣忕闂竴�?瀵逛竴鍙版病寮€娓告垙鐨勬満鍣ㄦ潵璇存槸寮傚父琛屼负瀵嗗害銆?
 */
/* ---- ���֡: ����ʧ��ʱҲдһ֡, ֻ��֡ͷ, players ȫ 0 ----
 * flags = 0xDEAD0000 | �׶���, ������������֪��������һ��:
 *   1 = ctx_open ʧ��(û������Ϸ/ƫ��û����)
 *   2 = sample_frame ʧ��(�� last_error)
 *   9 = off.valid == 0 (ƫ����δ����)
 */
static u32 g_diag_last_ms;
static u32 g_diag_last_code = 0xFFFFFFFFu;

static void write_diag(cs2_ctx* c, u32 code)
{
    u8* f;
    u32* h;
    u64 now;
    if (c == NULL || c->frame == NULL) { return; }
    /* The section channel needs no file handle; only the optional mirror does. */
#if CS2_FILE_MIRROR
    if (!cs2_file_ready()) { return; }
#endif

    /* 心跳限速（1Hz + ±300ms 抖动）：
     * 旧实现"没游戏时每一轮都尝试写"，虽然被 cs2_file 的 60Hz 上限挡了一半，
     * 但仍然是"每秒固定一次 10KB 覆盖写"这种可被 ETW/Minifilter 认出来的节拍。
     * 同一诊断码在 1 秒内不重复写；换码（1->2->9）立即写，状态机不丢。 */
    now = KeQueryInterruptTime() / 10000ull;
    if (code == g_diag_last_code)
    {
        u32 jit = (u32)((KeQueryInterruptTime() >> 12) % 600u);
        if ((u32)now - g_diag_last_ms < (1000u + jit)) { return; }
    }
    g_diag_last_code = code;
    g_diag_last_ms = (u32)now;
    f = c->frame;
    /* Diagnostic frames use the same stable-frame contract as real frames. */
    RtlZeroMemory(f, CS2_FRAME_SIZE);
    h = (u32*)f;
    h[0] = 0x45533243u;      /* magic 'C2SE' */
    h[1] = 5u;               /* ABI version */
    h[2] = 1u;               /* seq: ������д, ������̶�������� 1 Ҳ�� */
    h[3] = 0xDEAD0000u | (code & 0xFFFFu);
    now = KeQueryInterruptTime() / 10000ull;
    *(u64*)(f + 156) = now;
    h[2] = 2u;
    wr_u32(f, CS2_OFF_F_CHECKSUM,
           fnv1a(f + CS2_OFF_F_PLAYERS,
                 CS2CORE_MAX_PLAYERS * CS2_PLAYER_SIZE));
    /* Diagnostic frames use pid 0 (no target yet) so the display side can
     * verify the shared-memory channel while the game is closed. */
    cs2_shm_publish(c->t.pid, f, CS2_FRAME_SIZE);
#if CS2_FILE_MIRROR
    cs2_file_write(f, CS2_FRAME_SIZE);
#endif
}
/* 整帧一次 attach: 把"每字段一次 MmCopyVirtualMemory" 换成
 * "一帧一次地址空间切换" (见 cs2_ptread.c)。
 * attach 失败（进程正在退出）就退回逐次 MmCopyVirtualMemory 的老路径，功能不降级。 */
static int sample_frame(cs2_ctx* c, u8* frame)
{
    int ok;

    if (c != NULL && c->t.process != NULL && cs2_pt_attach(c->t.process) == CS2_OK) {
        cs2_pt_note_frame(1);
        ok = sample_frame_impl(c, frame);
        cs2_pt_detach();
        return ok;
    }
    cs2_pt_note_frame(0);
    return sample_frame_impl(c, frame);
}

static VOID sampler_thread(PVOID context)
{
    cs2_ctx* c = (cs2_ctx*)context;
    LARGE_INTEGER delay;
    u32 backoff_ms = 1000u;
    u32 idle_ticks = 0;      /* 场上无活人的连续次数，用来触发降频 */
    u32 i;

    /* 必须先给一个"相对超时"的初值：循环里走 continue 的路径（还没打开游戏）
     * 会直接拿 delay 去等，未初始化时是栈垃圾——正数会被当成绝对时间，
     * 结果就是"开机只写一帧然后永远卡住"（v2.2.2/2.2.3 的现象）。 */
    delay.QuadPart = -(LONGLONG)(10000000ull / (u64)(c->hz ? c->hz : 120));

    if (c->frame == NULL) {
        cs2_file_init();
        c->frame = (u8*)cs2_alloc_np(CS2_FRAME_SIZE_V3);
        if (c->frame != NULL) { RtlZeroMemory(c->frame, CS2_FRAME_SIZE_V3); }
    }

    /* v2.2.2 隐蔽性：不再用"固定 8.33ms 一次"的死节拍。
     * 反作弊可以靠采样周期/系统调用密度认出"有个东西在稳定地读游戏"；
     * 这里做两件事：
     *   1) 每次的等待时间加 0~33% 的抖动（抖动源用 KeQueryInterruptTime，
     *      不引入任何随机数 API，也不会被当成随机数调用特征）；
     *   2) 场上没有活人（菜单/回合间）时自动降到 1/3 频率，减少无效读取。 */
    while (g_running) {
        /* 游戏重启/切图后旧句柄会失效，若不主动重连就会一直卡在阶段 9。
         * 这里每帧检查一次：连续采样失败够多（或读取全失败）就立即 close，
         * 由循环开头重新 open —— 实测 1 秒内可恢复。 */
        if (c->opened && c->consec_fail > 30) {
            ctx_close(c);
            c->consec_fail = 0;
            c->off.valid = 0;
            c->valid_fail = 0;
        }
        if (!c->opened) {
            static u64 next_open_ms;
            u64 now_ms = KeQueryInterruptTime() / 10000ull;
            if (now_ms < next_open_ms) {
                KeWaitForSingleObject(&g_stop_ev, Executive, KernelMode, FALSE, &delay);
                continue;
            }
            next_open_ms = now_ms + (u64)backoff_ms;
            if (ctx_open(c) != CS2_OK) {
                write_diag(c, 1);
                cs2_channel_idle((u32)(KeQueryInterruptTime() / 10000ull));
                if (backoff_ms < 8000u) { backoff_ms *= 2u; }
                KeWaitForSingleObject(&g_stop_ev, Executive, KernelMode, FALSE, &delay);
                continue;
            }
            backoff_ms = 1000u;
        }

        /* 鍋忕Щ娌″�? 閲嶆柊瑙ｆ�?(鎵弿鍣ㄤ細鎹竴涓€欓€? */
        if (!c->off.valid && c->valid_fail >= CS2_RESCAN_AFTER) {
            cs2_auto_resolve(&c->t, &c->client, &c->engine, &c->off);
            /* 循环内的重解析同样会把 valid 清零，这里必须一起恢复，
             * 否则每 30 帧就被打回阶段 9。 */
            c->off.valid = 1;
            c->st.rescan_count++;
            c->st.build_number = c->off.build_number;
            c->valid_fail = 0;
        }

        if (c->frame != NULL && c->off.valid) {
            if (sample_frame(c, c->frame) == CS2_OK) {
                c->consec_fail = 0;
                /* 唯一出口: 一帧定长的整块覆盖 (原子性由长度恒定 + seqlock 帧头保证)。
                 * 主通道 = 共享内存 section: 内核建、显示端只读映射, 无设备/无 IOCTL/
                 * 无文件句柄传递。文件镜像默认关闭, 需要排障时用 -DMirror 打开。 */
                cs2_shm_publish(c->t.pid, c->frame, CS2_FRAME_SIZE_V3);
#if CS2_FILE_MIRROR
                cs2_file_write(c->frame, CS2_FRAME_SIZE_V3);
#endif
                c->st.frames++;
            } else {
                write_diag(c, 2);
                c->consec_fail++;
                c->st.failed_reads++;
                c->valid_fail++;
                c->off.valid = 0;      /* 涓嬩竴杞厛閲嶉�?*/
            }
        } else {
            /* 关键：这里也要累加 consec_fail。否则游戏重启后、进程句柄失效时，
             * 永远走不到 ctx_close → 不会重新打开新进程 → 一直卡在阶段 9。
             * 10 = 帧缓冲/文件句柄未就绪；9 = 偏移未启用 */
            write_diag(c, (c->frame == NULL) ? 10u : 9u);
            c->consec_fail++;
            c->valid_fail++;
        }
        if (c->consec_fail > CS2_MAX_CONSEC_FAIL) ctx_close(c);

        /* 鐘舵€佷�? 甯ч€氶亾杩樻病寤鸿捣鏉ユ�? 鐢ㄦ埛鎬侀潬杩欏洓涓綅灏辫兘瀹氫綅鍗″湪
         * "娌℃懜鍒版父�?/ 鍋忕Щ娌″�?/ 璇诲彇澶辫触 / 宸插彂甯? 鍝竴姝ャ€?
         * 5Hz 鑺傛�? 涓嶅崰甯﹀�? 鎶ュ憡娈典笉瀛樺湪鏃?cs2_env_diag_bits 鐩存帴杩斿洖�?*/
        {
            static u32 diag_tick;
            if ((++diag_tick % 30u) == 0u) {
                u32 set = 0, clr = 0;
                if (c->opened)          { set |= CS2ENV_FLAG_DIAG_GAME;  } else { clr |= CS2ENV_FLAG_DIAG_GAME;  }
                if (c->off.valid)       { set |= CS2ENV_FLAG_DIAG_OFFSET;} else { clr |= CS2ENV_FLAG_DIAG_OFFSET;}
                if (c->st.frames > 0)   { set |= CS2ENV_FLAG_DIAG_FRAMES;} else { clr |= CS2ENV_FLAG_DIAG_FRAMES;}
                if (c->consec_fail > 0) { set |= CS2ENV_FLAG_DIAG_FAILING;} else { clr |= CS2ENV_FLAG_DIAG_FAILING;}
                cs2_env_diag_bits(set, clr);
            }
        }

        /* ---- 动态节拍：空闲降频 + 抖动 ---- */
        {
            u32 base_hz = c->hz ? c->hz : 120u;
            u32 eff_hz;
            u64 now_t = KeQueryInterruptTime();
            u32 base_ticks;
            u32 jitter;

            if (c->opened && c->off.valid && idle_ticks > 60u) { eff_hz = base_hz / 3u; }
            else { eff_hz = base_hz; }
            if (eff_hz < 24u) eff_hz = 24u;

            base_ticks = 10000000u / eff_hz;
            if (base_ticks == 0u) base_ticks = 80000u;
            jitter = (u32)(now_t % (u64)(base_ticks / 3u + 1u));
            delay.QuadPart = -(LONGLONG)(base_ticks + jitter);

            if (c->opened && c->off.valid) {
                u32 pc = 0;
                if (c->frame != NULL) { pc = *(u32*)(c->frame + CS2_OFF_F_PLAYERCNT); }
                if (pc <= 1u) { if (idle_ticks < 100000u) idle_ticks++; }
                else { idle_ticks = 0; }
            } else {
                idle_ticks = 0;
            }
        }

        KeWaitForSingleObject(&g_stop_ev, Executive, KernelMode, FALSE, &delay);
    }

    for (i = 0; i < 8; i++) { /* 绛夊彂甯冩柟鏀跺熬 */
        LARGE_INTEGER tiny;
        tiny.QuadPart = -10000;
        KeWaitForSingleObject(&g_stop_ev, Executive, KernelMode, FALSE, &tiny);
    }

    if (c->frame != NULL) {
        /* 先擦再还：池块被复用之前，里面还完整躺着一帧世界状态 */
        RtlZeroMemory(c->frame, CS2_FRAME_SIZE_V3);
        ExFreePoolWithTag(c->frame, CS2_POOL_TAG);
        c->frame = NULL;
    }
    /* 上下文里也有模块基址/进程指针，一起擦掉 */
    RtlZeroMemory(&g_ctx, sizeof(g_ctx));
}

static WORK_QUEUE_ITEM g_work;
static void cs2_work_routine(PVOID ctx)
{
    sampler_thread(ctx);
    InterlockedExchange(&g_worker_queued, 0);
    KeSetEvent(&g_worker_done_ev, IO_NO_INCREMENT, FALSE);
}

NTSTATUS cs2_sampler_start(u32 hz)
{
    if (InterlockedCompareExchange(&g_worker_queued, 0, 0) != 0) {
        return STATUS_DEVICE_BUSY;
    }
    RtlZeroMemory(g_sc, sizeof(g_sc));
    RtlZeroMemory(g_grnd_saved, sizeof(g_grnd_saved));
    g_tail_items = 0;
    g_tick = 0;
    RtlZeroMemory(&g_ctx, sizeof(g_ctx));
    g_ctx.hz = hz ? hz : 144;
    g_ctx.local_slot = -1;
    KeInitializeEvent(&g_stop_ev, NotificationEvent, FALSE);
    KeInitializeEvent(&g_worker_done_ev, NotificationEvent, FALSE);
    InterlockedExchange(&g_running, 1);
    InterlockedExchange(&g_worker_queued, 1);
    ExInitializeWorkItem(&g_work, cs2_work_routine, &g_ctx);
    ExQueueWorkItem(&g_work, DelayedWorkQueue);
    return STATUS_SUCCESS;
}

void cs2_sampler_stop(void)
{
    InterlockedExchange(&g_running, 0);
    KeSetEvent(&g_stop_ev, IO_NO_INCREMENT, FALSE);
    if (InterlockedCompareExchange(&g_worker_queued, 0, 0) != 0) {
        KeWaitForSingleObject(&g_worker_done_ev, Executive, KernelMode, FALSE, NULL);
    }
    RtlZeroMemory(g_sc, sizeof(g_sc));
    RtlZeroMemory(g_grnd_saved, sizeof(g_grnd_saved));
    g_tail_items = 0;
}

void cs2_sampler_stats_get(cs2_sampler_stats* out)
{
    if (out) *out = g_ctx.st;
}

