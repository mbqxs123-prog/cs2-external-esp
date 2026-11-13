/* ============================================================================
 * sigscan.h  --  IDA 风格特征码扫描 (带通配 + RIP 相对寻址解析)
 *
 * 这是"自动解析坐标"的第一层: 只负责在 client.dll 的映像里找到指令,
 * 具体怎么从命中点推出偏移由 offsets.c 里的 op 描述。
 * ========================================================================== */
#ifndef CS2_SIGSCAN_H
#define CS2_SIGSCAN_H

#include "cs2core_km.h"

#define CS2_SIG_MAX_BYTES 64     /* 单个特征码最长字节数 */
#define CS2_SIG_MAX_HITS  16     /* 单个特征码最多记录多少命中(用于多候选判别) */

/* 命中结果 */
typedef struct cs2_sig_hit {
    u64  address;        /* 命中指令的绝对 VA (在目标进程地址空间) */
    u32  rva;            /* 相对模块基址 */
} cs2_sig_hit;

typedef struct cs2_sig_result {
    int          count;
    cs2_sig_hit  hits[CS2_SIG_MAX_HITS];
} cs2_sig_result;

/* 编译期把 "48 8B 05 ?? ?? ?? ?? 48 85 C0" 解析成字节+掩码。
 * 每次 resolve 前调用一次, 结果可缓存。 */
typedef struct cs2_pattern {
    u8   bytes[CS2_SIG_MAX_BYTES];
    u8   mask[CS2_SIG_MAX_BYTES];
    u32  len;
    int  valid;
} cs2_pattern;

int  cs2_pattern_compile(const char* text, cs2_pattern* out);

/* 在 [base, base+size) 的节范围内扫描。sections_only=1 时只扫可执行节,
 * 这是默认(更快, 且 .text 才是代码)。 */
int  cs2_sig_scan(cs2_target* t, u64 moduleBase, u32 moduleSize,
                  const cs2_pattern* pat, int maxHits, cs2_sig_result* out);

/* 只扫可执行节的版本 */
int  cs2_sig_scan_text(cs2_target* t, const cs2_module* mod,
                       const cs2_pattern* pat, int maxHits, cs2_sig_result* out);

/* 从命中点解析 RIP 相对位移:
 *   instrEnd = hit + instrLen        (指令末尾)
 *   target   = instrEnd + *(i32*)(hit + dispOffset)
 * dispOffset 通常是 3 (对 "48 8B 05 ?? ?? ?? ??" 这种 7 字节指令)。 */
int  cs2_sig_rel32(cs2_target* t, u64 hit, u32 dispOffset, u32 instrLen, u64* out);

/* 便捷组合: 扫 -> 取第一个命中 -> 解 relative -> 可选多次解引用。
 *   extraDerefs: 0 = 直接用解析出的地址; 1 = 再读一次指针 (对 dwEntityList 常见)
 *   derefSkip:   解引用时跳过的字节数(例如全局是 [ptr][pad8])
 */
int  cs2_sig_resolve(cs2_target* t, const cs2_module* mod, const char* patternText,
                     u32 dispOffset, u32 instrLen, u32 extraDerefs, u32 derefSkip,
                     u64* out, u64* hitAddressOut);

#endif /* CS2_SIGSCAN_H */
