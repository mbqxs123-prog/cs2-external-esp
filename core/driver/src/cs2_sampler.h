/* ============================================================================
 * cs2_sampler.h  --  采样线程 (Stage-A 的主循环)
 * ========================================================================== */
#ifndef CS2_SAMPLER_H
#define CS2_SAMPLER_H

#include "../inc/cs2core_km.h"

/* 启动/停止采样线程。hz 建议 120..250; 0 表示默认 144。 */
NTSTATUS cs2_sampler_start(u32 hz);
void     cs2_sampler_stop(void);

/* 采样统计 (给环境自检/诊断用, 不落盘) */
typedef struct cs2_sampler_stats {
    u32 frames;
    u32 failed_reads;
    u32 entities_last;
    u32 rescan_count;
    u32 build_number;
    u32 last_error;      /* 0 = 正常; 1 = 找不到进程; 2 = 偏移没对上; 3 = 发布失败 */
} cs2_sampler_stats;

void cs2_sampler_stats_get(cs2_sampler_stats* out);

#endif /* CS2_SAMPLER_H */
