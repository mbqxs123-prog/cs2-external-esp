/* ============================================================================
 * cs2_cave.c  --  代码洞寄生: 探洞 -> 试写 -> 执行验证 -> 可回滚
 *
 * === 要解决的问题 ===
 * 手工映射的驱动代码住在池里, 这些可执行页不属于任何已加载模块的镜像区间。
 * unKover 这类工具扫内核 PTE: Present && !NX && 不在任何 [ImageBase, +SizeOfImage)
 * 内 -> 直接判定 "hidden kernel code"。这是本方案唯一无法靠"少留痕迹"绕开的判据。
 *
 * 唯一正解是让代码物理上位于某个合法模块的地址范围内。本文件做的是这件事的
 * **前置判定 + 最小验证**: 找到已签名驱动 .text 里的对齐填充洞, 验证它
 *   (a) 可写  (b) 可执行  (c) 可原样还原
 * 三条都成立, 才谈得上把代码搬进去。
 *
 * === 为什么分三步而不是直接搬 ===
 * 直接做完整镜像 stomping(整份 PE 展开 + 重定位 + 导入解析) 要改几百处 RIP 相对
 * 寻址; 任何一处算错就是 0x50/0x1A 蓝屏, 而蓝屏在实战里等于当场暴露。先用
 * 16 字节 stub 把 "洞能写、能跑、能还原" 验证掉, 失败点会停在最早的不可逆操作之前。
 *
 * === 风险(必须知道) ===
 * 洞里的 0x00/0xCC 只是"看起来像填充"。如果那段字节其实被该驱动的某条跳转
 * 路径用到, 写入即崩。这是本方法的固有风险, 无法用静态扫描排除 —— 只能靠
 * 每加一个候选模块就实测一次, 且必须能回滚。
 *
 * === 编译开关 ===
 * 默认**不参与编译**(build-driver.ps1 不带 -Cave 时本文件不进镜像)。
 * 生产镜像里不该有这段代码: 它会引入 IoAllocateMdl/MmProbeAndLockPages 等导入,
 * 而正常采集驱动不需要它们。
 * ========================================================================== */
#include "cs2core_km.h"
#include "cs2_cave.h"

#ifdef CS2_ENABLE_CAVE

/* ntifs.h 在这个 WDK 版本里没有原型; 隐式 int 声明等于赌 ABI, 显式写一次。 */
NTKERNELAPI NTSTATUS ZwQuerySystemInformation(ULONG SystemInformationClass,
                                              PVOID SystemInformation,
                                              ULONG SystemInformationLength,
                                              PULONG ReturnLength);

/* ------------------------------------------------------------------ 参数 */

#define CAVE_MIN_BYTES   4096u          /* 至少 4KB 连续空洞才值得搬 */
#define CAVE_SCAN_LIMIT  (8u << 20)     /* 每个模块最多扫 8MB 的 .text */
#define CAVE_STUB_MAX    32u            /* 本轮只验证 16 字节机器码 */
#define CAVE_SAVE_MAX    32u            /* 回滚副本 */

/* 候选: 常驻、.text 大、有明显对齐填充。
 * dxgkrnl 在装了显卡驱动的机器上一定有; Wdf01000 几乎总在; win32kbase 在桌面会话里在。 */
static const char* g_targets[] = { "dxgkrnl.sys", "Wdf01000.sys", "win32kbase.sys" };
#define CAVE_TARGET_COUNT (sizeof(g_targets) / sizeof(g_targets[0]))

/* 探洞/验证结果。addr==0 表示没有可用洞。 */
typedef struct {
    u64  addr;              /* 洞起始 VA */
    u32  size;              /* 洞长度(连续 0x00/0xCC 字节数) */
    u64  mod_base;
    u32  mod_size;
    char mod[32];           /* 所属模块名(系统模块名, 非敏感) */
    u8   saved[CAVE_SAVE_MAX];
    u32  saved_len;
    int  claimed;           /* 1 = 当前洞内是我们的 stub */
} cs2_cave_slot;

static cs2_cave_slot g_cave;

/* 8 字节 stub: mov eax, 0x45533243 ('CS2E'); ret
 * 位置无关、不引用任何导入、不改任何寄存器以外的状态。 */
static const u8 g_stub[] = {
    0xB8, 0x43, 0x32, 0x53, 0x45,    /* mov eax, 0x45533243 */
    0xC3                             /* ret */
};
#define CAVE_STUB_LEN (sizeof(g_stub))
#define CAVE_MAGIC    0x45533243u

/* ------------------------------------------------------------ 报告(开发期) */

/* 只在 -Cave 开发构建里写。生产构建不含本文件, 因此不存在这条落盘路径。 */
static void cave_report(const char* s, u32 n)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    path;
    IO_STATUS_BLOCK   iosb;
    HANDLE            h = NULL;
    LARGE_INTEGER     off;
    static const WCHAR buf[] = L"\\??\\C:\\Windows\\Temp\\cc.log";

    RtlInitUnicodeString(&path, L"\\??\\C:\\Windows\\Temp\\cc.log");
    (void)buf;
    InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_APPEND_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                 FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN_IF,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0)))
        return;
    off.QuadPart = 0;
    (void)ZwWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)s, n, NULL, NULL);
    ZwClose(h);
}

static void cave_say(const char* a, const char* b, u64 v)
{
    char line[128];
    u32 n = 0, i;
    while (a[n] && n < 40) { line[n] = a[n]; n++; }
    if (b) { i = 0; while (b[i] && n < 90) { line[n++] = b[i++]; } }
    line[n++] = ' ';
    { char t[20]; u32 k = 0, j;
      if (v == 0) t[k++] = '0';
      else { while (v) { u32 d = (u32)(v & 0xF); t[k++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v >>= 4; } }
      for (j = 0; j < k; j++) line[n++] = t[k - 1 - j]; }
    line[n++] = '\r'; line[n++] = '\n';
    cave_report(line, n);
}

/* ------------------------------------------------------------ 名字比较 */

static int cave_name_eq(const char* a, const char* b)
{
    u32 i;
    for (i = 0; a[i] && b[i]; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return (a[i] == 0 && b[i] == 0) ? 1 : 0;
}

static int cave_name_contains(const char* hay, const char* needle)
{
    const char* p = hay;
    while (*p) {
        if (cave_name_eq(p, needle)) return 1;
        p++;
    }
    return 0;
}

/* ------------------------------------------------------------ 扫一段区间 */

/* 返回第一个 >= need 字节的连续 0x00/0xCC 区间起点, 找不到返回 0。只读。 */
static u64 cave_scan_range(u64 start, u32 size, u32 need)
{
    u64 va = start, end = start + size;
    u32 run = 0;
    u64 run_start = 0;
    u8  probe[16];

    while (va + sizeof(probe) <= end) {
        memcpy(probe, (const void*)va, sizeof(probe));
        int all_zero = 1, all_cc = 1, k;
        for (k = 0; k < 16; k++) {
            if (probe[k] != 0x00) all_zero = 0;
            if (probe[k] != 0xCC) all_cc = 0;
        }
        if (all_zero || all_cc) {
            if (run == 0) run_start = va;
            run += 16;
            if (run >= need) return run_start;
        } else {
            run = 0;
        }
        va += 16;
    }
    return 0;
}

/* ==========================================================================
 * 阶段 1: 探洞(纯只读)
 * ========================================================================== */
int cs2_cave_probe(void)
{
    ULONG     need = 0;
    NTSTATUS  st;
    PVOID     buf;
    u32       count, i;

    RtlZeroMemory(&g_cave, sizeof(g_cave));

    st = ZwQuerySystemInformation(11 /* SystemModuleInformation */, NULL, 0, &need);
    if (need == 0) { cave_say("probe: size fail", NULL, 0); return CS2_FAIL; }

    buf = cs2_alloc_np((SIZE_T)need + 0x1000);
    if (buf == NULL) { cave_say("probe: alloc fail", NULL, 0); return CS2_FAIL; }

    st = ZwQuerySystemInformation(11, buf, need + 0x1000, &need);
    if (!NT_SUCCESS(st)) {
        cs2_free_wipe(buf, need + 0x1000);
        cave_say("probe: query fail", NULL, 0);
        return CS2_FAIL;
    }

    count = *(u32*)buf;
    cave_say("probe: modules", NULL, count);

    for (i = 0; i < count && i < 512; i++) {
        /* sizeof(RTL_PROCESS_MODULE_INFORMATION) = 0x128 on x64 */
        u8*  m          = (u8*)buf + 8 + (u64)i * 0x128;
        u64  image_base = *(u64*)(m + 0x10);
        u32  image_size = *(u32*)(m + 0x18);
        char* name      = (char*)(m + 0x28);
        u32  k;
        int  hit = 0;

        for (k = 0; k < CAVE_TARGET_COUNT; k++) {
            if (cave_name_contains(name, g_targets[k])) { hit = 1; break; }
        }
        if (!hit) continue;
        if (image_size < 0x1000 || image_size > (256u << 20)) continue;

        /* 解析 PE 拿 .text */
        {
            u8* nt = (u8*)image_base;
            u32 lfanew;
            u8* pe;
            u16 nsec, optsz;
            u8* sec;
            u32 s;

            if (nt[0] != 'M' || nt[1] != 'Z') continue;
            lfanew = *(u32*)(nt + 0x3C);
            if (lfanew == 0 || lfanew > 0x1000) continue;
            pe = nt + lfanew;
            if (*(u32*)pe != 0x00004550) continue;

            nsec  = *(u16*)(pe + 6);
            optsz = *(u16*)(pe + 0x14);
            sec   = pe + 0x18 + optsz;

            for (s = 0; s < nsec && s < 32; s++) {
                u8* e = sec + s * 40;
                u32 vsize, rva, chars;
                u64 cave;

                if (e[0] != '.' || e[1] != 't' || e[2] != 'e' || e[3] != 'x' || e[4] != 't') continue;

                vsize = *(u32*)(e + 8);
                rva   = *(u32*)(e + 12);
                chars = *(u32*)(e + 36);
                if (vsize == 0) continue;
                if (vsize > CAVE_SCAN_LIMIT) vsize = CAVE_SCAN_LIMIT;
                if ((chars & 0x20000000u) == 0) continue;      /* 只要可执行 */

                cave = cave_scan_range(image_base + rva, vsize, CAVE_MIN_BYTES);
                if (cave != 0) {
                    u32 n = 0;
                    g_cave.addr     = cave;
                    g_cave.size     = CAVE_MIN_BYTES;
                    g_cave.mod_base = image_base;
                    g_cave.mod_size = image_size;
                    while (name[n] && n < 31) { g_cave.mod[n] = name[n]; n++; }
                    g_cave.mod[n] = 0;
                    cave_say("HIT ", g_cave.mod, cave);
                    cs2_free_wipe(buf, need + 0x1000);
                    return CS2_OK;
                }
                break;   /* 每个模块只看第一个 .text */
            }
        }
    }

    cs2_free_wipe(buf, need + 0x1000);
    cave_say("probe: no cave", NULL, 0);
    return CS2_FAIL;
}

/* ==========================================================================
 * 写只读代码页: MDL 映射, 不动 CR0.WP
 *
 * 关 CR0.WP 是能被检测的(某些 AC 轮询 CR0 位), 而且一关就得关中断, 时间窗不好控。
 * IoAllocateMdl + MmProbeAndLockPages + MmMapLockedPagesSpecifyCache 会给同一批
 * 物理页建一份新的可写映射, 原 PTE 不变 —— 写完撤映射, 页属性回到原样。
 * ========================================================================== */
static int cave_write(u64 va, const u8* src, u32 len)
{
    PMDL  mdl;
    PVOID map;
    int   ok = 0;

    mdl = IoAllocateMdl((PVOID)va, len, FALSE, FALSE, NULL);
    if (mdl == NULL) { cave_say("write: mdl fail", NULL, 0); return 0; }

    __try {
        MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        IoFreeMdl(mdl);
        cave_say("write: lock fail", NULL, 0);
        return 0;
    }

    map = MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (map == NULL) {
        cave_say("write: map fail", NULL, 0);
    } else {
        __try {
            RtlCopyMemory(map, src, len);
            ok = 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = 0;
        }
        MmUnmapLockedPages(map, mdl);
    }

    MmUnlockPages(mdl);
    IoFreeMdl(mdl);
    return ok;
}

static int cave_read(u64 va, u8* dst, u32 len)
{
    int ok = 0;
    __try {
        RtlCopyMemory(dst, (const void*)va, len);
        ok = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    return ok;
}

/* ==========================================================================
 * 阶段 2 + 3: 试写 1 字节 -> 写 stub -> 执行 -> 还原
 *
 * 每一步都留回滚副本。任何一步失败, 洞必须回到原始字节 —— 残留一个被改过
 * 的填充区, 比"有 unbacked 页"更容易被镜像完整性校验抓到。
 * ========================================================================== */
int cs2_cave_claim(void)
{
    u8  back[CAVE_STUB_LEN];
    u8  probe_byte;
    u8  orig_byte;
    u32 i;
    typedef u32 (*cave_fn)(void);
    cave_fn fn;
    u32 rv = 0;

    if (g_cave.addr == 0) {
        if (cs2_cave_probe() != CS2_OK) return CS2_FAIL;
    }
    if (g_cave.claimed) return CS2_OK;

    if (!cave_read(g_cave.addr, g_cave.saved, CAVE_STUB_LEN)) {
        cave_say("claim: unreadable", NULL, 0);
        return CS2_FAIL;
    }
    g_cave.saved_len = CAVE_STUB_LEN;

    /* --- 步 1: 写单个字节再读回。失败在这里停, 损失最小。 --- */
    orig_byte  = g_cave.saved[0];
    probe_byte = (u8)(orig_byte ^ 0x5A);
    if (!cave_write(g_cave.addr, &probe_byte, 1)) {
        cave_say("claim: not writable", NULL, 0);
        return CS2_FAIL;
    }
    {
        u8 chk = 0;
        cave_read(g_cave.addr, &chk, 1);
        if (chk != probe_byte) {
            cave_write(g_cave.addr, &orig_byte, 1);        /* 还原 */
            cave_say("claim: write did not land", NULL, 0);
            return CS2_FAIL;
        }
    }
    /* 还原这一字节, 下一步整体写 */
    cave_write(g_cave.addr, &orig_byte, 1);

    /* --- 步 2: 写完整 stub --- */
    cave_read(g_cave.addr, back, CAVE_STUB_LEN);        /* 二次确认原始字节 */
    for (i = 0; i < CAVE_STUB_LEN; i++) {
        if (back[i] != g_cave.saved[i]) {
            cave_say("claim: content changed", NULL, 0);
            return CS2_FAIL;
        }
    }
    if (!cave_write(g_cave.addr, g_stub, CAVE_STUB_LEN)) {
        cave_say("claim: stub write fail", NULL, 0);
        cave_write(g_cave.addr, g_cave.saved, CAVE_STUB_LEN);
        return CS2_FAIL;
    }

    /* --- 步 3: 执行。stub 只做 mov eax,imm32; ret, 不碰栈以外任何东西 --- */
    fn = (cave_fn)(ULONG_PTR)g_cave.addr;
    __try {
        rv = fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rv = 0;
    }

    if (rv != CAVE_MAGIC) {
        /* 执行失败 = 这个洞不能用于放代码。原样还原。 */
        cave_write(g_cave.addr, g_cave.saved, CAVE_STUB_LEN);
        cave_say("claim: exec fail rv", NULL, rv);
        return CS2_FAIL;
    }

    g_cave.claimed = 1;
    cave_say("claim: OK exec in ", g_cave.mod, g_cave.addr);
    return CS2_OK;
}

/* 还原洞内原始字节。真卸载/换版本前必须调用。 */
void cs2_cave_release(void)
{
    if (g_cave.addr == 0 || !g_cave.claimed || g_cave.saved_len == 0) return;
    (void)cave_write(g_cave.addr, g_cave.saved, g_cave.saved_len);
    {
        u8 chk = 0;
        if (cave_read(g_cave.addr, &chk, 1) && chk == g_cave.saved[0]) {
            cave_say("release: restored", NULL, 0);
        } else {
            cave_say("release: STILL PATCHED", NULL, g_cave.addr);
        }
    }
    g_cave.claimed = 0;
}

/* 洞地址, 供后续搬迁逻辑定位 */
u64 cs2_cave_addr(void) { return g_cave.addr; }

#else  /* !CS2_ENABLE_CAVE ------------------------------------------------ */

/* 生产构建: 接口保留, 全部空实现。镜像里没有 MDL 相关导入。 */
int  cs2_cave_probe(void)   { return CS2_FAIL; }
int  cs2_cave_claim(void)   { return CS2_FAIL; }
void cs2_cave_release(void) { }
u64  cs2_cave_addr(void)    { return 0; }

#endif /* CS2_ENABLE_CAVE */
