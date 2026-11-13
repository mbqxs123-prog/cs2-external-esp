/* ============================================================================
 * cs2_auto.h  --  运行期"自动解析坐标/偏移"引擎 (用户需求 m00405)
 *
 * 设计原则 (为什么不靠特征码, 也不靠写死的偏移):
 *   CS2 每次更新都会挪动 dw* 锚点, 但**数据结构的形状不变**:
 *     - viewMatrix 永远是 16 个 float, 且最后一行是 (0,0,0,1);
 *     - 实体系统永远挂在 client.dll 的一个 .data 槽位上, 槽位 -> chunk 数组 ->
 *       实体对象, 每层都是"指向堆的指针";
 *     - 本地玩家控制器永远有 m_bIsLocalPlayerController == 1。
 *   所以本引擎不匹配字节, 而是**扫描 client.dll 的可写数据段, 用这些结构
 *   不变量把锚点重新找回来**。找不到就退回 cs2_seed.h 的种子值, 找到的种子
 *   值还会再被同一套校验筛一遍 —— 三个来源(扫描 / 种子 / schema)互为兜底。
 *
 * 结果缓存在 cs2_offsets 里, 只有校验失败才会重新解析, 所以每局只付一次代价。
 * ========================================================================== */
#ifndef CS2_AUTO_H
#define CS2_AUTO_H

#include "../inc/cs2core_km.h"
#include "cs2_seed.h"

/* 锚点来源位 */
#define CS2_SRC_SEED     0x01u   /* 来自 cs2_seed.h */
#define CS2_SRC_SCAN     0x02u   /* 运行期结构扫描命中 */
#define CS2_SRC_LIVE     0x04u   /* 语义校验通过 */

typedef struct cs2_offsets {
    /* --- client.dll 锚点 (模块相对偏移) --- */
    u32 dwEntityList;
    u32 dwLocalPlayerController;
    u32 dwLocalPlayerPawn;
    u32 dwViewMatrix;
    u32 dwGameRules;
    u32 dwGlobalVars;

    /* --- engine2.dll 锚点 --- */
    u32 dwNetworkGameClient;
    u32 dwBuildNumber;
    u32 dwWindowWidth;
    u32 dwWindowHeight;

    /* --- 运行期标定出来的东西 --- */
    u32 bone_pos_off;        /* CTransform 里位置字段的类内偏移: 0x10 或 0x00 */
    u32 build_number;        /* engine2 dwBuildNumber 的值 (版本指纹, 只用于上报) */
    u32 src_mask;            /* CS2_SRC_* 位: 哪些锚点是扫出来的 */
    u32 entity_shift;        /* 实体系统分块参数, 默认 CS2_ENT_CHUNK_SHIFT */
    u32 entity_chunk_mul;    /* 默认 CS2_ENT_CHUNK_MUL  */
    u32 entity_chunk_off;    /* 默认 CS2_ENT_CHUNK_OFF  */
    u32 entity_stride;       /* 默认 CS2_ENT_STRIDE     */
    u32 entity_index_mask;   /* 默认 CS2_ENT_INDEX_MASK */
    u32 valid;               /* 1 = 已解析且通过校验 */
} cs2_offsets;

/* 初始化: 先把种子值灌进去 */
void cs2_auto_init(cs2_offsets* o);

/* 完整解析: 扫描 client.dll / engine2.dll 的可写段, 找回锚点并逐项校验。
 * 返回 CS2_OK 表示至少 viewMatrix + entityList + 一个活实体三项都过了;
 * 返回 CS2_FAIL 表示这版完全没对上(此时把种子值也标为不可信)。 */
int cs2_auto_resolve(cs2_target* t, const cs2_module* client, const cs2_module* engine,
                     cs2_offsets* o);

/* 单独校验: 这套偏移在当前这一帧能不能读出合理数据 */
int cs2_auto_validate(cs2_target* t, const cs2_offsets* o, u64 client_base,
                      u32* out_entities, u32* out_local_slot);

/* 骨骼位置字段标定 (0x10 / 0x00 二选一), 用一个真实 pawn 打分。成功返回所选偏移 */
u32 cs2_auto_bone_offset(cs2_target* t, const cs2_offsets* o, u64 scene_node);

/* 实体列表解引用 (返回 0 表示索引无效) */
u64 cs2_entity_at(cs2_target* t, const cs2_offsets* o, u64 client_base, i32 index);

/* 取本地玩家控制器 (找到返回其实体地址, 同时填 local_slot) */
u64 cs2_local_controller(cs2_target* t, const cs2_offsets* o, u64 client_base, i32* local_slot);

/* 校验一个 4x4 矩阵像不像 CS2 的 view-projection (单位阵/垃圾全拒) */
int cs2_auto_vm_ok(const f32* m);

#endif /* CS2_AUTO_H */
