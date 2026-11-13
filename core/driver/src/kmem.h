/* ============================================================================
 * kmem.h  --  无句柄内存读取 (见 docs/ANTICHEAT.md §3.3)
 *
 * 为什么不直接用 ZwOpenProcess/ReadProcessMemory:
 *   - OpenProcess 会在 cs2.exe 上留下 PROCESS_VM_READ 句柄, 反作弊枚举句柄就能看到;
 *   - NtReadVirtualMemory 是 EAC/BE/VAC 类反作弊最常 inline hook / 挂 ETW 的路径。
 * 我们只拿 EPROCESS(MmCopyVirtualMemory 只需要 PEPROCESS), 不建句柄。
 * ========================================================================== */
#ifndef CS2_KMEM_H
#define CS2_KMEM_H

#include "cs2core_km.h"

/* 按镜像名在 cs2.exe 的模块列表里找模块。注意: 这里读的是 PEB->Ldr,
 * 而不是 ZeLoadedModules 快照 —— 前者是目标进程自己的视角, 能看到
 * client.dll 等游戏模块。 */
int cs2_module_find(cs2_target* t, const char* name, cs2_module* out);

/* 大小写不敏感比较(内核里没有 _stricmp 的可靠版本, 自己写) */
int cs2_streq_ci(const char* a, const char* b);

/* 手动页表遍历读 (MmCopyVirtualMemory 的补充路径):
 *   - 用于批量读骨骼这种高频小读, 省掉每次的地址校验开销;
 *   - 只在 PTE 有效且页是驻留的情况下直接读, 否则返回 CS2_FAIL 交回上层。
 * 需要先 cs2_target_open 拿到 EPROCESS。 */
int cs2_read_fast(cs2_target* t, u64 va, void* dst, u32 len);

#endif /* CS2_KMEM_H */
