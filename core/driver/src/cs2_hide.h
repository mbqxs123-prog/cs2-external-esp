/* ============================================================================
 * cs2_hide.h  --  自我隐藏 (摘链)
 * ========================================================================== */
#ifndef CS2_HIDE_H
#define CS2_HIDE_H

#include "../inc/cs2core_km.h"

/* 立刻摘链 (仅用于加载流程完全结束之后) */
void cs2_hide_self(PDRIVER_OBJECT drv);

/* 延迟摘链: 在 DriverEntry 里只登记, 由采样线程在 N 毫秒后真正执行。
 * 原因: DriverEntry 返回前摘链会和内核自己的收尾工作(释放 INIT 段、
 * 处理三条 LDR 链)打架, 那是最典型的"驱动一加载就蓝屏"。 */
void cs2_hide_defer(PDRIVER_OBJECT drv);

/* 采样线程每轮调用一次; 到点就执行, 只执行一次 */
void cs2_hide_poll(void);

/* 线程启动地址伪装: 在采样线程入口调用一次, 把本线程 ETHREAD->StartAddress
 * 改成 ntoskrnl 里的真实函数, 让"启动地址不在模块内的线程"这条判据失效。 */
void cs2_hide_thread_start(PVOID our_routine);

#endif /* CS2_HIDE_H */
