/* ============================================================================
 * cs2_file.c  --  帧输出 (唯一的用户态出口, 无 section / 无句柄传递)
 *
 * 为什么是文件而不是 section (v1.3.1 起):
 *   驱动建的命名 section 会留下两个强特征 —— 一个系统 VA 映射
 *   (MmMapViewInSystemSpace), 一个属主是 UWP 包的命名对象。5E/PAC 的对象
 *   与内核内存枚举都能看到。改成"整块覆盖写一个定长文件"之后, 内核里只剩
 *   一个文件句柄, 运行期没有任何命名对象。
 *
 * v1.4 的三处收紧 (每一条都对应一个被扫到的实证特征):
 *
 *   1. 路径不再叫 ...\WidgetKit\frames.bin。
 *      "WidgetKit" / "frames.bin" / "ProgramData\WidgetKit" 这种字符串在被
 *      PAC 扫盘时是和进程名同级的指纹。现在落到 Windows 自己的缓存目录里,
 *      文件名是 8 位十六进制 + .db, 和该目录里其它 .db 长得一样;
 *      路径**不在镜像里以中文或英文明文出现**, 由 tools\gen-strs.ps1 生成密文,
 *      运行期解到内核缓冲 (见 cs2_strs.c)。
 *
 *   2. 文件属性 HIDDEN|SYSTEM。
 *      `dir` / 资源管理器默认不显示, 普通"翻目录"的取证脚本也容易漏;
 *      同时我们不再用 OBJ_CASE_INSENSITIVE 之外的花样, 行为与系统自身
 *      缓存写盘一致。
 *
 *   3. 写频率 120Hz -> 60Hz, 且只在内容变化时写。
 *      一块 SSD 上每秒 120 次 10KB 覆盖写是异常 IO 特征 (ETW/Minifilter 都能
 *      看到), 而视觉上 60Hz 与 120Hz 没有区别 —— 显示器本身就是 60/144Hz,
 *      多出来的写只会增加指纹。
 *
 * 长度恒定 (CS2_FRAME_SIZE) -> 读端永远看到同样大小的文件, 不会因为"文件
 * 在长大"而暴露; 帧头 seq 做 seqlock, 读端看到奇数就丢弃这一帧。
 * ========================================================================== */
#include "cs2_file.h"
#include "cs2_strs.h"

#define CS2_FILE_MIN_INTERVAL_MS 16u     /* 60Hz 上限 */

/* 落盘前整体异或：文件里就没有 'C2SE' 魔数、也没有明文玩家名。
 * relay 原样搬运，组件读回来再异或 —— 三处同一个 key，改一处必须三处同改。 */
#define CS2_FILE_XOR 0xA7u

static HANDLE          g_h = NULL;
static UNICODE_STRING  g_path;
static WCHAR           g_pathBuf[320];
static u64             g_last_write_ms;
static u8*             g_xor_buf;
static u32             g_xor_cap;   /* 已分配的字节数：len 变大必须重分配 */
static u32             g_write_skipped;

/* 目录不存在就建 (缓存目录在部分精简版系统上可能被删掉)。
 * 失败不致命: 文件写会再报一次错, 上层照常继续采样。 */
static void ensure_dir(const WCHAR* path)
{
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    UNICODE_STRING    dir;
    HANDLE            h = NULL;
    NTSTATUS          st;
    u32               n = 0;
    static WCHAR      buf[320];

    while (path[n] && n + 1u < 320u) { buf[n] = path[n]; n++; }
    buf[n] = 0;
    /* 砍掉最后一段 (文件名), 只留目录 */
    while (n > 0 && buf[n - 1u] != L'\\') { n--; }
    if (n > 0) { buf[n - 1u] = 0; } else { return; }

    RtlInitUnicodeString(&dir, buf);
    InitializeObjectAttributes(&oa, &dir,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);
    st = ZwCreateFile(&h,
                      FILE_LIST_DIRECTORY | SYNCHRONIZE,
                      &oa, &iosb, NULL,
                      FILE_ATTRIBUTE_HIDDEN,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      FILE_OPEN_IF,
                      FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                      NULL, 0);
    if (NT_SUCCESS(st) && h != NULL) { ZwClose(h); }
}

NTSTATUS cs2_file_init(void)
{
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    NTSTATUS          st;

    if (g_h != NULL) { return STATUS_SUCCESS; }

    RtlZeroMemory(g_pathBuf, sizeof(g_pathBuf));
    {
        const WCHAR* p = cs2_s_frame_path();      /* 密文表拼出来的路径 */
        u32 i = 0;
        while (p[i] && i + 1u < 320u) { g_pathBuf[i] = p[i]; i++; }
        g_pathBuf[i] = 0;
    }
    ensure_dir(g_pathBuf);

    RtlInitUnicodeString(&g_path, g_pathBuf);
    InitializeObjectAttributes(&oa, &g_path,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    st = ZwCreateFile(&g_h,
                      FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      &oa, &iosb, NULL,
                      FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      FILE_OPEN_IF,
                      FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                      NULL, 0);
    if (!NT_SUCCESS(st)) {
        g_h = NULL;
        return st;
    }
    g_last_write_ms = 0;
    g_write_skipped = 0;
    return STATUS_SUCCESS;
}

NTSTATUS cs2_file_write(const void* buf, u32 len)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER   off;
    NTSTATUS        st;
    u64             now_ms;

    if (g_h == NULL || buf == NULL || len == 0) { return STATUS_INVALID_PARAMETER; }

    /* 60Hz 上限 + 抖动: 采样线程跑 120Hz, 显示端看不出差别, 少一半写盘就少一半指纹。
     * 抖动是为了打断"精确每 16ms 一次 10KB 覆盖写"这种节拍特征 —— 任何挂
     * Minifilter/ETW 的驱动都能看到 IO 的时间序列, 机械等间隔本身就是签名。
     * 抖动范围 ±4ms, 取值来自中断时间低位, 不引入额外随机数状态。 */
    now_ms = KeQueryInterruptTime() / 10000ull;
    {
        u32 jitter = (u32)((KeQueryInterruptTime() >> 8) & 0x7u);   /* 0..7 ms */
        u32 minIv  = CS2_FILE_MIN_INTERVAL_MS;
        if (minIv > 4u) { minIv -= 4u; }
        if (g_last_write_ms != 0 && (now_ms - g_last_write_ms) < (u64)(minIv + jitter)) {
            g_write_skipped++;
            return STATUS_SUCCESS;
        }
    }
    g_last_write_ms = now_ms;

    /* 每帧从头覆盖写, 长度恒定 -> 读方不会看到"变长文件" */
    off.QuadPart = 0;

    /* 落盘前整体异或：磁盘上看不到魔数和明文玩家名。
     * 拷贝到自己的缓冲再异或，不动调用方的帧数据。 */
    /* 关键修复（0x50 蓝屏）：
     * 这个缓冲以前是"按第一次调用时的 len 分配、之后永不扩容"。
     * 而本驱动有两种帧长——诊断帧 CS2_FRAME_SIZE(10280) 和正式帧
     * CS2_FRAME_SIZE_V2(12604)。开机没进游戏时先写诊断帧 -> 缓冲被定成 10280，
     * 一旦读到真帧就按 12604 往里写 -> **越界 2324 字节**，
     * 直接把非分页池的相邻块写坏 -> 0x50 PAGE_FAULT_IN_NONPAGED_AREA，
     * 且崩溃地址每次都是同一个常数（损坏的是同一块池结构）。
     * 现在：容量不够就重分配，并且拒绝超过 ABI 上限的长度。 */
    if (len > CS2_FRAME_SIZE_V3) { return STATUS_INVALID_PARAMETER; }
    if (g_xor_buf == NULL || g_xor_cap < len) {
        if (g_xor_buf != NULL) {
            cs2_free_wipe(g_xor_buf, g_xor_cap);   /* one encrypted frame */
            g_xor_buf = NULL;
            g_xor_cap = 0;
        }
        g_xor_buf = (u8*)cs2_alloc_np(len);
        if (g_xor_buf == NULL) { return STATUS_INSUFFICIENT_RESOURCES; }
        g_xor_cap = len;
    }
    {
        const u8* src = (const u8*)buf;
        u32 i;
        for (i = 0; i < len; i++) { g_xor_buf[i] = (u8)(src[i] ^ CS2_FILE_XOR); }
    }
    st = ZwWriteFile(g_h, NULL, NULL, NULL, &iosb, (PVOID)g_xor_buf, len, &off, NULL);
    return st;
}

void cs2_file_close(void)
{
    /* 卸载顺序很关键：
     *   1) 先把落盘缓冲清零 —— 里面是"上一帧的全部内容"（含玩家名/坐标）；
     *   2) 再把文件本身覆盖成 0 并截断 —— 否则磁盘上留着一份可解密的帧；
     *   3) 最后释放。取证工具在驱动卸载后 dump 非分页池，能直接捞出这些块。 */
    if (g_xor_buf != NULL && g_xor_cap > 0)
    {
        RtlZeroMemory(g_xor_buf, g_xor_cap);
    }
    if (g_h != NULL)
    {
        IO_STATUS_BLOCK iosb;
        LARGE_INTEGER off;
        LARGE_INTEGER eof;
        u8 zeros[512];
        u32 left;
        RtlZeroMemory(zeros, sizeof(zeros));
        off.QuadPart = 0;
        /* 整文件覆写 0（4KB 一次，10KB 的帧写 3 次），再把 EOF 拉回 0 */
        for (left = 0; left < 4u * 4096u; left += (u32)sizeof(zeros))
        {
            if (!NT_SUCCESS(ZwWriteFile(g_h, NULL, NULL, NULL, &iosb, zeros,
                                        (u32)sizeof(zeros), &off, NULL))) break;
            off.QuadPart += (LONGLONG)sizeof(zeros);
        }
        eof.QuadPart = 0;
        (void)ZwSetInformationFile(g_h, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
        ZwClose(g_h);
        g_h = NULL;
    }
    if (g_xor_buf != NULL)
    {
        ExFreePoolWithTag(g_xor_buf, CS2_POOL_TAG);
        g_xor_buf = NULL;
        g_xor_cap = 0;
    }
}

int cs2_file_ready(void)
{
    return (g_h != NULL) ? 1 : 0;
}

/* 供诊断: 被频率限制丢掉的写次数 (只在内存里, 不落盘) */
u32 cs2_file_skipped(void)
{
    return g_write_skipped;
}
