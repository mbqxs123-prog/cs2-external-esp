/* ============================================================================
 * cs2core_abi.h  --  Stage-A(采集核心) 与 Stage-B(显示端) 之间唯一的二进制契约
 *
 * 设计原则
 *   1. 只读单向数据流: Stage-A 写, Stage-B 读。Stage-B 永远不碰 CS2 进程。
 *   2. 固定布局 + 固定偏移, 不依赖编译器 ABI: 两端都用 pack(4) / 手工偏移解析,
 *      由 static_assert / 运行期 assert 双边校验, 避免结构体悄悄漂移。
 *   3. seqlock 无锁: 采集端可能在做半帧时被读, 读端靠 seq 奇偶 + 二次读校验重试,
 *      不需要任何互斥体(互斥体是反作弊容易枚举的同步对象)。
 *   4. 骨骼使用"语义槽位"而非 CS2 原始骨骼号: 采集端负责把任意模型的骨架映射到
 *      17 个固定槽位, 显示端完全不关心模型差异。
 *
 * v4 相对 v3 的改动 (全部是"贴合精度/同步"需要, 只追加, 不移动既有偏移):
 *   - 帧头新增 viewport 矩形: 游戏渲染结果在桌面上占据的物理像素矩形。
 *     显示端不再假设"游戏填满整屏", 于是窗口化 / 4:3 拉伸 / 黑边信箱
 *     三种情况下映射都精确, 这是透视框贴合的最主要误差来源。
 *   - 帧头新增 sampleTickMs: 采样时刻的 GetTickCount64, 显示端用它算数据年龄,
 *     对移动目标做速度外推, 让骨骼与目标实际位置同步而不滞后一个采样周期。
 *   - 玩家记录新增 velocity: 单位/秒, 世界坐标速度, 用于上面的外推。
 * ========================================================================== */
#ifndef CS2CORE_ABI_H
#define CS2CORE_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* --- 帧魔数: 小端字节序为 'C','2','S','E' ------------------------------- */
#define CS2CORE_MAGIC        0x45533243u
#define CS2CORE_VERSION      5u   /* v5: 玩家记录追加 武器/弹药/护甲/装备位 */

/* --- 容量上限 ------------------------------------------------------------ */
#define CS2CORE_MAX_PLAYERS  32
#define CS2CORE_MAX_BONES    17
#define CS2CORE_NAME_LEN     32
#define CS2CORE_MAP_LEN      32

/* --- 语义骨骼槽位 (bones[] 的下标) --------------------------------------- */
enum {
    CS2_BONE_HEAD       = 0,   /* head          */
    CS2_BONE_NECK       = 1,   /* neck_0        */
    CS2_BONE_CHEST      = 2,   /* spine_2 上胸  */
    CS2_BONE_STOMACH    = 3,   /* spine_1       */
    CS2_BONE_PELVIS     = 4,   /* pelvis        */
    CS2_BONE_SHLD_L     = 5,   /* arm_upper_L   */
    CS2_BONE_ELBOW_L    = 6,   /* arm_lower_L   */
    CS2_BONE_HAND_L     = 7,   /* hand_L        */
    CS2_BONE_SHLD_R     = 8,   /* arm_upper_R   */
    CS2_BONE_ELBOW_R    = 9,   /* arm_lower_R   */
    CS2_BONE_HAND_R     = 10,  /* hand_R        */
    CS2_BONE_HIP_L      = 11,  /* leg_upper_L   */
    CS2_BONE_KNEE_L     = 12,  /* leg_lower_L   */
    CS2_BONE_FOOT_L     = 13,  /* ankle_L       */
    CS2_BONE_HIP_R      = 14,  /* leg_upper_R   */
    CS2_BONE_KNEE_R     = 15,  /* leg_lower_R   */
    CS2_BONE_FOOT_R     = 16,  /* ankle_R       */
    CS2_BONE_COUNT      = 17
};

/* --- 玩家标志位 ---------------------------------------------------------- */
#define CS2_PF_ALIVE         0x00000001u
#define CS2_PF_LOCAL         0x00000002u   /* 本地玩家 (Stage-B 一般不画自己) */
#define CS2_PF_DORMANT       0x00000004u   /* m_bDormant: 数据不可信 */
#define CS2_PF_TEAMMATE      0x00000008u
#define CS2_PF_SPOTTED       0x00000010u   /* m_bSpotted 被队友点亮 */
#define CS2_PF_VISIBLE       0x00000020u   /* 本地射线遮挡检测通过(可选)     */
#define CS2_PF_DEFUSING      0x00000040u
#define CS2_PF_BOMB_CARRIER  0x00000080u

/* 装备位 (CS2_OFF_P_GEAR 字节里的位) */
#define CS2_GEAR_HELMET      0x01u   /* 有头盔 */
#define CS2_GEAR_DEFUSER     0x02u   /* 有拆弹器 */
#define CS2_GEAR_ARMORED     0x04u   /* 有甲(护甲值 > 0) */
#define CS2_GEAR_ZOOMED      0x08u   /* 正在开镜 */
#define CS2_GEAR_DEFUSING    0x10u   /* 正在拆弹 */
#define CS2_GEAR_RELOADING   0x20u   /* 正在换弹 */

/* --- 帧状态 -------------------------------------------------------------- */
#define CS2_FS_IN_GAME       0x00000001u
#define CS2_FS_ROUND_LIVE    0x00000002u

/* ==========================================================================
 * 布局常量 —— 两边都必须使用这些常量, 不允许各自 sizeof 出来用
 * ======================================================================== */
#define CS2_OFF_F_MAGIC        0     /* u32 */
#define CS2_OFF_F_VERSION      4     /* u32 */
#define CS2_OFF_F_SEQ          8     /* u32  seqlock: 奇数=正在写 */
#define CS2_OFF_F_FLAGS       12     /* u32 */
#define CS2_OFF_F_TICK        16     /* u64  游戏 tick, 仅供调试/对齐 */
#define CS2_OFF_F_VIEWMAT     24     /* f32[16] world->clip, CS2 dwViewMatrix 约定 */
#define CS2_OFF_F_SCREENW     88     /* i32  渲染目标宽(游戏内部渲染分辨率) */
#define CS2_OFF_F_SCREENH     92     /* i32  渲染目标高 */
#define CS2_OFF_F_LOCALSLOT   96     /* i32 */
#define CS2_OFF_F_PLAYERCNT  100     /* u32 */
#define CS2_OFF_F_MAP        104     /* char[32] */
#define CS2_OFF_F_CHECKSUM   136     /* u32  对 players 区间做 FNV-1a */
#define CS2_OFF_F_VIEWPORTX  140     /* i32  渲染结果在桌面上的物理像素矩形 */
#define CS2_OFF_F_VIEWPORTY  144     /* i32  全 0 表示"未知" => 显示端退回整屏映射 */
#define CS2_OFF_F_VIEWPORTW  148     /* i32 */
#define CS2_OFF_F_VIEWPORTH  152     /* i32 */
#define CS2_OFF_F_SAMPLETICK 156     /* u64  GetTickCount64(采样时刻, 与显示端同一时钟) */
#define CS2_OFF_F_FOV        164     /* f32  当前视野(度)，0=未知；开镜时会变小 */
#define CS2_OFF_F_PLAYERS    168     /* cs2_player_t[] */
#define CS2_FRAME_HEADER_SZ  168
#define CS2_FRAME_SIZE       (CS2_FRAME_HEADER_SZ + CS2CORE_MAX_PLAYERS * CS2_PLAYER_SIZE)

/* 单个玩家记录 */
#define CS2_OFF_P_SLOT         0     /* u32 */
#define CS2_OFF_P_TEAM         4     /* u32  2=T 3=CT */
#define CS2_OFF_P_HEALTH       8     /* i32 */
#define CS2_OFF_P_LIFESTATE   12     /* i32  0=alive */
#define CS2_OFF_P_FLAGS       16     /* u32  CS2_PF_* */
#define CS2_OFF_P_BONEVALID   20     /* u32  位掩码, bit i => bones[i] 有效 */
#define CS2_OFF_P_NAME        24     /* char[32] UTF-8 */
#define CS2_OFF_P_ORIGIN      56     /* f32[3] 世界坐标(脚底) */
#define CS2_OFF_P_EYEPOS      68     /* f32[3] 眼睛世界坐标 */
#define CS2_OFF_P_EYEANGLES   80     /* f32[2] pitch, yaw */
#define CS2_OFF_P_DISTANCE    88     /* f32  与本地玩家的米数 */
#define CS2_OFF_P_BONES       92     /* f32[17][3] 语义骨骼世界坐标 */
#define CS2_OFF_P_VELOCITY   296     /* f32[3] 世界速度, 单位/秒 (0 表示不知道) */
#define CS2_OFF_P_WEAPON     308     /* u16  当前武器 item definition index (0=未知) */
#define CS2_OFF_P_AMMO       310     /* u16  当前弹匣余弹 */
#define CS2_OFF_P_ARMOR      312     /* u8   护甲值 0..100 */
#define CS2_OFF_P_GEAR       313     /* u8   CS2_GEAR_* 位 */
#define CS2_OFF_P_RESERVED3  314     /* u16  对齐保留 */
#define CS2_PLAYER_SIZE      316

/* 编译期自检 —— C/C++ 侧把任何布局漂移变成编译错误 */
#define CS2CORE_STATIC_ASSERT(cond, tag) typedef char cs2_assert_##tag[(cond) ? 1 : -1]
CS2CORE_STATIC_ASSERT(CS2_PLAYER_SIZE == 316, player_size_316);
CS2CORE_STATIC_ASSERT(CS2_FRAME_HEADER_SZ == 168, header_size_168);
CS2CORE_STATIC_ASSERT(CS2_FRAME_SIZE == 10280, frame_size_10280);

/* ==========================================================================
 * v6 尾部扩展 (追加在 CS2_FRAME_SIZE 之后, 不动任何既有偏移)
 *
 *   [0, 10280)        原来的 v5 帧 (老读端照旧只读这一段)
 *   [10280, 12604)    v6 尾部: 每玩家的碰撞盒 + 地面物品列表
 *
 * 为什么追加而不是改 PlayerSize:
 *   改 stride 会让所有既有读端(覆盖层/离线工具/老组件)整体错位;
 *   追加尾部则是"新读端能多读就多读, 老读端无视即可"。
 *
 *   v6 布局:
 *     0     u32 magic 'TAIL' (0x4C494154)
 *     4     u32 version = 1
 *     8     u32 aabbMask     bit i => aabb[i] 有效
 *     12    u32 itemCount    0..64
 *     16    f32 aabb[32][6]  minX,minY,minZ,maxX,maxY,maxZ  (世界坐标/CS2 units)
 *     784   item[64] x 24B
 *     2320  u32 checksum     FNV-1a over [0,2320)
 * ======================================================================== */
#define CS2CORE_TAIL_MAGIC      0x4C494154u   /* 'TAIL' */
#define CS2CORE_TAIL_VERSION    1u
#define CS2CORE_ITEM_MAX        64
#define CS2CORE_ITEM_SIZE       24

#define CS2_OFF_T_MAGIC         0
#define CS2_OFF_T_VERSION       4
#define CS2_OFF_T_AABBMASK      8
#define CS2_OFF_T_ITEMCNT       12
#define CS2_OFF_T_AABB          16
#define CS2_TAIL_AABB_STRIDE    24            /* 6 x f32 */
#define CS2_OFF_T_ITEMS         (CS2_OFF_T_AABB + CS2CORE_MAX_PLAYERS * CS2_TAIL_AABB_STRIDE)  /* 784 */
#define CS2_OFF_T_TAILCHECK     (CS2_OFF_T_ITEMS + CS2CORE_ITEM_MAX * CS2CORE_ITEM_SIZE)       /* 2320 */
#define CS2_TAIL_SIZE           (CS2_OFF_T_TAILCHECK + 4)                                     /* 2324 */
#define CS2_FRAME_SIZE_V2       (CS2_FRAME_SIZE + CS2_TAIL_SIZE)                              /* 12604 */

/* item 记录字段 */
#define CS2_OFF_I_POS           0     /* f32[3] 世界坐标 */
#define CS2_OFF_I_ID            12    /* u16 item definition index */
#define CS2_OFF_I_CAT           14    /* u8  CS2_ITEM_CAT_* */
#define CS2_OFF_I_FLAGS         15    /* u8  CS2_ITEM_F_* */
#define CS2_OFF_I_AMMO          16    /* u16 弹匣/数量 */
#define CS2_OFF_I_RESERVED      18    /* u16 */
#define CS2_OFF_I_RESERVED2     20    /* u32 */

#define CS2_ITEM_CAT_WEAPON     0
#define CS2_ITEM_CAT_GRENADE    1
#define CS2_ITEM_CAT_C4         2
#define CS2_ITEM_CAT_KIT        3
#define CS2_ITEM_CAT_OTHER      4

#define CS2_ITEM_F_ON_GROUND    0x01u
#define CS2_ITEM_F_HAS_AMMO     0x02u
#define CS2_ITEM_F_PLANTED      0x04u
#define CS2_ITEM_F_DEFUSE_KIT   0x08u
#define CS2_ITEM_F_PROJECTILE   0x10u

CS2CORE_STATIC_ASSERT(CS2_TAIL_SIZE == 2324, tail_size_2324);
CS2CORE_STATIC_ASSERT(CS2_FRAME_SIZE_V2 == 12604, frame_size_v2_12604);
#undef CS2CORE_STATIC_ASSERT

/* ==========================================================================
 * v7 尾部 (追加在 v6 尾部之后, 同样不动任何既有偏移)
 *
 *   [0, 10280)        v5 帧
 *   [10280, 12604)    v6 尾部: 碰撞盒 + 地面物品
 *   [12604, 13968)    v7 尾部: 飞行中的投掷物 + 燃烧区(火焰)
 *
 * 布局:
 *   0     u32 magic 'GRND' (0x444E5247)
 *   4     u32 version = 1
 *   8     u32 count
 *   12    u32 flags  (bit0 = 本帧有投掷物数据)
 *   16    rec[24] × 56B
 *   1360  u32 checksum  FNV-1a over [0,1360)
 * ======================================================================== */
#define CS2CORE_GRND_MAGIC      0x444E5247u   /* 'GRND' */
#define CS2CORE_GRND_VERSION    1u
#define CS2CORE_GREN_MAX        24
#define CS2CORE_GREN_SIZE       56

#define CS2_OFF_G_MAGIC         0
#define CS2_OFF_G_VERSION       4
#define CS2_OFF_G_COUNT         8
#define CS2_OFF_G_FLAGS         12
#define CS2_OFF_G_RECS          16
#define CS2_OFF_G_CHECKSUM      (CS2_OFF_G_RECS + CS2CORE_GREN_MAX * CS2CORE_GREN_SIZE)  /* 1360 */
#define CS2_GRND_TAIL_SIZE      (CS2_OFF_G_CHECKSUM + 4)                                  /* 1364 */
#define CS2_FRAME_SIZE_V3       (CS2_FRAME_SIZE_V2 + CS2_GRND_TAIL_SIZE)                  /* 13968 */

/* 记录字段 */
#define CS2_OFF_R_POS           0     /* f32[3] 当前世界坐标 */
#define CS2_OFF_R_VEL           12    /* f32[3] 当前速度(单位/秒) */
#define CS2_OFF_R_INITPOS       24    /* f32[3] 出手点(服务器侧) */
#define CS2_OFF_R_RADIUS        36    /* f32 爆炸/燃烧半径(单位) */
#define CS2_OFF_R_DETONATE      40    /* f32 起爆/熄灭 的游戏时间(秒) */
#define CS2_OFF_R_SPAWN         44    /* f32 出生游戏时间(秒) */
#define CS2_OFF_R_ID            48    /* u16 物品定义号(投掷物可读时为 43..49, 否则 0) */
#define CS2_OFF_R_KIND          50    /* u8  CS2_GREN_KIND_* */
#define CS2_OFF_R_FLAGS         51    /* u8  CS2_GREN_F_* */
#define CS2_OFF_R_EXTRA         52    /* u32 kind=proj: 弹跳次数; kind=fire: 火焰点个数 */

#define CS2_GREN_KIND_PROJ      0     /* 飞行中的投掷物 */
#define CS2_GREN_KIND_FIRE      1     /* 燃烧区(燃烧瓶/燃烧弹) */

#define CS2_GREN_F_LIVE         0x01u
#define CS2_GREN_F_BOUNCED      0x02u
#define CS2_GREN_F_SMOKE        0x04u

#define CS2_GREN_T_UNKNOWN      0
#define CS2_GREN_T_HE           1
#define CS2_GREN_T_FLASH        2
#define CS2_GREN_T_SMOKE        3
#define CS2_GREN_T_MOLOTOV      4
#define CS2_GREN_T_DECOY        5

#define CS2CORE_STATIC_ASSERT(cond, tag) typedef char cs2_assert_##tag[(cond) ? 1 : -1]
CS2CORE_STATIC_ASSERT(CS2_GRND_TAIL_SIZE == 1364, grnd_size_1364);
CS2CORE_STATIC_ASSERT(CS2_FRAME_SIZE_V3 == 13968, frame_size_v3_13968);
#undef CS2CORE_STATIC_ASSERT

/* ==========================================================================
 * 结构体镜像 (真正写内存用的是上面的偏移常量; 结构体只为了方便代码阅读)
 * ======================================================================== */
#pragma pack(push, 4)

typedef struct cs2_vec3 {
    float x, y, z;
} cs2_vec3;

typedef struct cs2_player {
    uint32_t slot;
    uint32_t team;
    int32_t  health;
    int32_t  lifeState;
    uint32_t flags;
    uint32_t boneValid;
    char     name[CS2CORE_NAME_LEN];
    float    origin[3];
    float    eyePos[3];
    float    eyeAngles[2];
    float    distance;
    float    bones[CS2CORE_MAX_BONES][3];
    float    velocity[3];
    uint16_t weapon;      /* item definition index */
    uint16_t ammo;        /* 弹匣余弹 */
    uint8_t  armor;       /* 0..100 */
    uint8_t  gear;        /* CS2_GEAR_* */
    uint16_t reserved3;
} cs2_player_t;

typedef struct cs2_frame {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;
    uint32_t flags;
    uint64_t tick;
    float    viewMatrix[16];
    int32_t  screenW;
    int32_t  screenH;
    int32_t  localSlot;
    uint32_t playerCount;
    char     mapName[CS2CORE_MAP_LEN];
    uint32_t checksum;
    int32_t  viewportX;
    int32_t  viewportY;
    int32_t  viewportW;
    int32_t  viewportH;
    uint64_t sampleTickMs;
    uint32_t reserved2;
    cs2_player_t players[CS2CORE_MAX_PLAYERS];
} cs2_frame_t;

#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(cs2_player_t) == CS2_PLAYER_SIZE, "player layout drift");
static_assert(sizeof(cs2_frame_t)  == CS2_FRAME_SIZE,  "frame layout drift");
static_assert(offsetof(cs2_player_t, velocity)  == CS2_OFF_P_VELOCITY, "velocity offset drift");
static_assert(offsetof(cs2_frame_t, viewMatrix) == CS2_OFF_F_VIEWMAT,  "viewmatrix offset drift");
static_assert(offsetof(cs2_frame_t, viewportX)  == CS2_OFF_F_VIEWPORTX, "viewport offset drift");
static_assert(offsetof(cs2_frame_t, players)    == CS2_OFF_F_PLAYERS,  "players offset drift");
}
#endif

/* ==========================================================================
 * Stage-A 采样要求 (显示端的贴合精度直接取决于这里)
 *
 *   1. viewMatrix 与所有玩家骨骼必须在【同一个游戏 tick 内】读出。
 *      分两次读会产生"框在漂"的经典症状: 相机已转, 坐标还是上一 tick 的。
 *   2. 读出后立刻写 viewport 与 sampleTickMs, 再置偶数 seq 发布。
 *   3. viewport 用游戏窗口 client 区域在桌面上的物理像素矩形; 若游戏有黑边,
 *      用实际渲染视口(即去黑边后的矩形), 而不是 client 矩形。
 *   4. velocity 直接取玩家的 m_vecVelocity; 拿不到就写 0 (显示端会自动关掉外推)。
 * ======================================================================== */

/* ==========================================================================
 * 共享通道
 *
 * 通道 A (默认, 低延迟): file-backed section
 *   名字默认 "Local\\_sm0_9b3f" —— 运行期可换成随机化名字,
 *   名字存于 overlay 配置, 双方启动时读取。名字前缀伪装成系统对象 (见 docs/ANTICHEAT.md)。
 *
 * 通道 B (备选): UDP loopback, 帧头魔数相同, 整帧一次性发送 (10024B 会分片, 无所谓)。
 *   无需任何命名内核对象, 但引入约 0.2-1ms 抖动。
 * ======================================================================== */
#define CS2CORE_CHANNEL_DEFAULT "Local\\_sm0_9b3f"

/* FNV-1a 32 (两端实现必须一致, 可自检: fnv1a("") == 0x811C9DC5) */
#define CS2CORE_FNV_BASIS 0x811C9DC5u
#define CS2CORE_FNV_PRIME 0x01000193u

#ifdef __cplusplus
extern "C" {
#endif
static __inline uint32_t cs2_fnv1a(const void* data, uint32_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint32_t h = CS2CORE_FNV_BASIS;
    while (len--) { h ^= *p++; h *= CS2CORE_FNV_PRIME; }
    return h;
}
#ifdef __cplusplus
}
#endif

#endif /* CS2CORE_ABI_H */
