/* ============================================================================
 * cs2_ptread.h  --  单次 attach + 裸拷贝 (见 cs2_ptread.c 顶部说明)
 * ========================================================================== */
#ifndef CS2_PTREAD_H
#define CS2_PTREAD_H

#include "cs2core_km.h"

/* 1 = 当前线程处于 attach 状态 */
int  cs2_pt_attached(void);

/* 切到目标进程地址空间。成功返回 CS2_OK；重入或参数为空返回 CS2_FAIL。 */
int  cs2_pt_attach(PEPROCESS proc);

/* 切回来。未 attach 时调用是空操作。 */
void cs2_pt_detach(void);

/* 在 attach 状态下读目标进程的用户态地址。失败返回 CS2_FAIL。 */
int  cs2_pt_read(u64 va, void* dst, u32 len);

/* 诊断构建: 记录快路径命中/回退次数, 并导出到 C:\Windows\Temp\cc.log。 */
void cs2_pt_note_frame(int attached);
#ifdef CS2_ENABLE_DIAG
void cs2_pt_dump_diag(void);
#else
#define cs2_pt_dump_diag() ((void)0)
#endif

#endif /* CS2_PTREAD_H */
