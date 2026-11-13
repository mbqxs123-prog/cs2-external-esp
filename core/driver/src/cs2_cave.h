/* ============================================================================
 * cs2_cave.h  --  代码洞寄生(探洞/试写/执行/还原), 见 cs2_cave.c
 *
 * 生产构建(不带 -Cave)下全部是空实现, 镜像里不会出现 MDL 相关导入。
 * ========================================================================== */
#ifndef CS2_CAVE_H
#define CS2_CAVE_H

#include "cs2core_km.h"

/* 阶段 1: 只读探洞。找到 >=4KB 的 .text 填充洞返回 CS2_OK。 */
int  cs2_cave_probe(void);

/* 阶段 2+3: 试写 1 字节 -> 写 stub -> 执行验证。任一步失败都还原。 */
int  cs2_cave_claim(void);

/* 还原洞内原始字节(卸载/换版本前调用)。 */
void cs2_cave_release(void);

/* 当前洞地址, 0 = 无 */
u64  cs2_cave_addr(void);

#endif /* CS2_CAVE_H */
