/* ============================================================================
 * cs2_hide.c  --  v1.4: 不再改写任何内核结构 (整个文件是"故意什么都不做")
 *
 * === 之前的做法与它的问题 ===
 * 旧实现做了两件事:
 *   1) 把 KLDR_DATA_TABLE_ENTRY 从三条链上摘掉、抹 DllBase/SizeOfImage;
 *      结果: 第三节链(InitializationOrder)指向不在同一链上的节点, 任何一次
 *      遍历 -> 0x139 CORRUPT_LIST_ENTRY。而且 SizeOfImage/DllBase 被系统用
 *      来算区间/走栈回溯, 清零比"被看见"严重得多。
 *   2) 在采样线程里扫自己 ETHREAD 的前 0x800 字节, 把等于本函数的指针改成
 *      nt! 里的地址, 伪装 ThreadStartAddress。
 *      结果: 这本身也是一种"内核结构被改写"的证据 —— 一旦 AC 侧做
 *      ETHREAD 一致性交叉校验(比如拿 StartAddress 与 KTHREAD->ApcState、
 *      与创建时记录的 StackBase/StackLimit 比对), 改动过的字段比"地址不在
 *      模块内"更显眼, 而且是**唯一**指向"有人在动内核"的物理证据。
 *
 * === 现在的做法 ===
 * 什么都不改。
 *   - 采样已经跑在 ExQueueWorkItem 的系统 worker 上 (见 cs2_sampler.c),
 *     启动地址天然在 ntoskrnl 内, 不需要伪装;
 *   - 驱动按"手工映射 + 不进 PsLoadedModuleList"的方式加载 (docs/STEALTH-5E.md
 *     路径 B), 所以模块链里本来就没有我们 —— 也就不需要摘链;
 *   - 不建 DeviceObject / 不注册符号链接 / 不建命名对象 (见 cs2_channel.c),
 *     对象管理器里没有任何我们的名字。
 *
 * 于是本文件保留接口、去掉全部"外科手术"。这样一个能在 5E 环境长期跑的
 * 驱动, 内核状态与它加载前**逐字节一致** —— 这是最抗审计的形态。
 * ========================================================================== */
#include "../inc/cs2core_km.h"
#include "cs2_hide.h"

void cs2_hide_defer(PDRIVER_OBJECT drv)
{
    (void)drv;      /* 无需延迟摘链: 手工映射本来就不在模块链里 */
}

void cs2_hide_poll(void)
{
    /* 每轮无事可做 */
}

void cs2_hide_self(PDRIVER_OBJECT drv)
{
    (void)drv;
}

/* 保留 API (采样端可能仍调用), 但不改写 ETHREAD。
 * 走 ExQueueWorkItem 之后这个调用点已被删除; 留着是为了不破坏旧构建脚本。 */
void cs2_hide_thread_start(PVOID our_routine)
{
    (void)our_routine;
}
