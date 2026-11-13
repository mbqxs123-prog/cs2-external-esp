/* ============================================================================
 * cs2_ptread.c  --  单次 attach + 裸拷贝的批量读路径
 *
 * === 解决的是什么问题 ===
 * 旧路径每帧对每个字段调一次 MmCopyVirtualMemory。一帧约 200~300 次
 * （10 个玩家 x 属性/骨骼/武器/物品扫描）。
 *
 * MmCopyVirtualMemory 是 ntoskrnl 的导出函数，反作弊侧可用的观测手段有三条：
 *   1) inline hook 该函数（很多 AC 驱动就这么干），统计"谁在调、调多密"；
 *   2) 以太网/ETW 的 KiFastCallEntry 统计（对内核内直接调用无效，但对 hook 有效）；
 *   3) 用 KeStackAttachProcess 的调用点做交叉验证 —— attach 到 cs2.exe 的线程
 *      如果再没有对应的合法业务（比如驱动自己的 IO），就是异常。
 *
 * 我们把"每帧几百次导出函数调用"压成"每帧一次 attach + 几百次裸 RtlCopyMemory"：
 *   - 导出函数调用密度从 ~3 万次/秒降到 ~240 次/秒（120Hz x 2）；
 *   - 不再有任何一次 MmCopyVirtualMemory 调用，hook 它的 AC 看不到我们；
 *   - 依然零句柄：不 OpenProcess、不 ObReferenceObjectByHandle、不建 PROCESS_VM_READ。
 *
 * === 诚实边界 ===
 * KeStackAttachProcess 同样是导出函数，hook 它的 AC 能看到"有个线程在切地址空间"。
 * 这条路降低的是**密度与调用面**，不是"完全不可见"。要彻底不可见只能自己做
 * CR3 切换 + 页表遍历（见 docs/DRIVER-STEALTH-3PHASE.md §4.4，本文件留了接入点
 * 但不默认开：改 CR3 在多核上要 flush TLB，写错一位就是 0x101/0x1A 蓝屏）。
 *
 * === 安全设计 ===
 *   - 只在采样 worker 线程里 attach；重入直接失败返回，不会叠两层 APC 状态；
 *   - 跨页按 4KB 切分，单页失败只丢该页，不污染已读到的前半段；
 *   - 每段拷贝走 MmCopyMemory（不抛异常）：目标页被换出/进程正在退出时退化为读失败。
 *     不用 __try/__except —— 手工映射的镜像没有 .pdata 登记，异常分发会失败，
 *     直接表现为 0x50 取指错误（本轮崩溃的根因之一）；
 *   - 地址范围严格限制在用户空间（< 0x7FFFFFFEFFFF），内核地址一律拒绝 ——
 *     否则给一个坏指针就能从内核态读任意内核内存，这是自己给自己开后门；
 *   - attach 失败（进程已在退出）自动回退旧路径，功能不降级。
 * ========================================================================== */
#include "cs2_ptread.h"

/* 采样 worker 是唯一会 attach 的线程；cs2_read 也只在这个线程里被调用。
 * 用 Interlocked 而不是裸 bool：万一将来有人在别的线程调 cs2_read，
 * 它读到的要么是"attach 中"、要么是"没 attach"，不会是中间态。 */
static volatile LONG g_pt_on;

static KAPC_STATE g_pt_apc;

#define CS2_USER_MAX 0x00007FFFFFFEFFFFull
#define CS2_READ_CAP (64ull << 20)      /* 单次读上限，防参数错误把内核拖死 */

int cs2_pt_attached(void)
{
    return (InterlockedCompareExchange(&g_pt_on, 0, 0) == 1) ? CS2_OK : CS2_FAIL;
}

/* ---- 诊断构建专用: 统计快路径是否真的生效 ---- */
#ifdef CS2_ENABLE_DIAG
static volatile LONG g_pt_ok_cnt;
static volatile LONG g_pt_fail_cnt;

void cs2_pt_note_frame(int attached)
{
    if (attached) InterlockedIncrement(&g_pt_ok_cnt);
    else          InterlockedIncrement(&g_pt_fail_cnt);
}
#else
void cs2_pt_note_frame(int attached) { (void)attached; }
#endif

/* ============================================================================
 * 快路径临时停用（2026-11-01）
 *
 * 症状：驱动在"没找到游戏"的状态下可以长期稳定运行（只写诊断帧），
 *       一旦 cs2.exe 启动、采样线程真的开始读游戏内存，系统 0x50 蓝屏，
 *       参数 4 = 2（取指令失败）。
 *
 * 已经排除的：
 *   - __try/__except：已全部换成 MmCopyMemory，镜像里不再依赖 .pdata 分发
 *   - 池越界：四处 cs2_free_wipe 的擦除长度都与分配长度一致
 *
 * 还没排除的只剩 KeStackAttachProcess 这条路本身（手工映射的镜像 + attach，
 * 与 MmCopyVirtualMemory 路径的唯一差异就在这里）。
 *
 * 所以这里直接返回失败 —— cs2_read() 会看到"不在 attach 态"，自动回退到
 * MmCopyVirtualMemory。代价是每帧多几百次导出调用（隐蔽性差一档），
 * 换来的是不蓝屏。定位到根因、或者改成自己切 CR3 之后再打开。
 * ========================================================================== */
#define CS2_PT_ATTACH_DISABLED 1

int cs2_pt_attach(PEPROCESS proc)
{
#if CS2_PT_ATTACH_DISABLED
    (void)proc;
    return CS2_FAIL;                    /* 永远回退到 MmCopyVirtualMemory */
#else
    if (proc == NULL) return CS2_FAIL;
    if (InterlockedCompareExchange(&g_pt_on, 1, 0) != 0) return CS2_FAIL;

    RtlZeroMemory(&g_pt_apc, sizeof(g_pt_apc));
    KeStackAttachProcess(proc, &g_pt_apc);
    return CS2_OK;
#endif
}

void cs2_pt_detach(void)
{
#if CS2_PT_ATTACH_DISABLED
    /* 不 attach 自然不需要 detach。顺带把 KeUnstackDetachProcess 的导入也去掉：
     * hook 这两个导出的 AC 连"有人在切地址空间"都看不到。 */
    return;
#else
    if (InterlockedCompareExchange(&g_pt_on, 0, 1) != 1) return;
    KeUnstackDetachProcess(&g_pt_apc);
    RtlZeroMemory(&g_pt_apc, sizeof(g_pt_apc));
#endif
}

/* 裸拷贝。调用前必须已在 attach 状态，否则读到的是当前地址空间 —— 数据全错。 */
int cs2_pt_read(u64 va, void* dst, u32 len)
{
    u8* out = (u8*)dst;
    u32 done = 0;

    if (dst == NULL || len == 0) return CS2_FAIL;
    if (va < 0x10000ull) return CS2_FAIL;                 /* 空指针附近不读 */
    if ((u64)len > CS2_READ_CAP) return CS2_FAIL;
    if (va > CS2_USER_MAX) return CS2_FAIL;               /* 只读用户空间 */
    if (va + (u64)len - 1ull > CS2_USER_MAX) return CS2_FAIL;

    while (done < len)
    {
        u64 cur = va + (u64)done;
        u32 page_left = 0x1000u - (u32)(cur & 0xFFFull);
        u32 chunk = len - done;
        if (chunk > page_left) chunk = page_left;         /* 不跨页，避免一页坏掉全段丢 */

        /* 这里绝对不能用 __try/__except。
         *
         * 手工映射的镜像没有向内核登记 .pdata（kdmapper 不调用
         * RtlInsertInvertedFunctionTable），所以 __except 在没有映射表的情况下
         * 无法工作：一旦读触发 #PF，异常找不到 handler，执行流会跳到垃圾地址，
         * 表现为 0x50 PAGE_FAULT_IN_NONPAGED_AREA 且参数 4 = 2（取指失败）。
         *
         * MmCopyMemory 不抛异常，失败只返回状态码，且同样是"拷字节"语义。
         * 每页一次，坏页丢掉、整段报失败（半帧数据比没有更危险）。 */
        {
            MM_COPY_ADDRESS src;
            SIZE_T copied = 0;
            NTSTATUS st;

            src.VirtualAddress = (PVOID)(ULONG_PTR)cur;
            st = MmCopyMemory(out + done, src, (SIZE_T)chunk, MM_COPY_MEMORY_VIRTUAL, &copied);
            if (!NT_SUCCESS(st) || copied != (SIZE_T)chunk) return CS2_FAIL;
        }
        done += chunk;
    }
    return CS2_OK;
}

/* ==========================================================================
 * 诊断构建(-Cave 会同时打开 CS2_ENABLE_DIAG): 把快路径命中率写到临时文件。
 *
 * 为什么不用帧头的一个位: flags 的高位已经被"失败诊断帧"占用了
 * (0xDEAD0000|code, 而 0xDEAD 的 bit31 本身就是 1), 再塞一个 bit 会让消费端
 * 把正常帧误判成诊断帧。宁可多一条只在诊断镜像里存在的落盘路径。
 * ========================================================================== */
#ifdef CS2_ENABLE_DIAG
void cs2_pt_dump_diag(void)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    path;
    IO_STATUS_BLOCK   iosb;
    HANDLE            h = NULL;
    LARGE_INTEGER     off;
    char              line[96];
    u32               n = 0;
    LONG              okv = InterlockedCompareExchange(&g_pt_ok_cnt, 0, 0);
    LONG              fv  = InterlockedCompareExchange(&g_pt_fail_cnt, 0, 0);
    const char*       p1  = "ptread: attach_ok=";
    const char*       p2  = " fallback=";
    u32               i;

    RtlInitUnicodeString(&path, L"\\??\\C:\\Windows\\Temp\\cc.log");
    InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_APPEND_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                 FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN_IF,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0)))
        return;

    for (i = 0; p1[i] && n < 60; i++) line[n++] = p1[i];
    for (i = 0; i < 31; i++) { LONG v = (okv >> (4 * (7 - i))) & 0xF; line[n++] = (char)(v < 10 ? '0' + v : 'a' + v - 10); }
    for (i = 0; p2[i] && n < 76; i++) line[n++] = p2[i];
    for (i = 0; i < 31; i++) { LONG v = (fv >> (4 * (7 - i))) & 0xF; line[n++] = (char)(v < 10 ? '0' + v : 'a' + v - 10); }
    line[n++] = '\r'; line[n++] = '\n';

    off.QuadPart = 0;
    (void)ZwWriteFile(h, NULL, NULL, NULL, &iosb, line, n, NULL, NULL);
    ZwClose(h);
}
#endif
