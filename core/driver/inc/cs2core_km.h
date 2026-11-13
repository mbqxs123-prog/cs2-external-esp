/* ============================================================================
 * cs2core_km.h  --  Stage-A 鍐呮牳閲囬泦鏍稿績鐨勫唴閮ㄥ叕鍏卞ご
 *
 * 绾﹀畾 (瑙?docs/ANTICHEAT.md):
 *   - 涓嶆墦浠讳綍鏃ュ織 (鏃?DbgPrint/KdPrint/WPP/ETW)銆傛墍鏈夎瘖鏂蛋甯ч噷鐨?flags/瀛楁銆? *   - 涓嶅垱寤烘寚鍚?cs2.exe 鐨勫彞鏌? 鍐呭瓨璇诲彇鍙蛋 PsLookupProcessByProcessId +
 *     MmCopyVirtualMemory 鎴栨墜鍔ㄩ〉琛ㄩ亶鍘嗐€? *   - 涓嶆敞鍐?ObRegisterCallbacks, 涓嶆寕 SSDT/IDT, 涓嶅仛 inline hook銆? * ========================================================================== */
#ifndef CS2CORE_KM_H
#define CS2CORE_KM_H

#include <ntifs.h>
#include <ntimage.h>

#include "cs2core_abi.h"     /* ../shared/cs2core_abi.h (宸ョ▼ IncludePath 宸插姞) */

/* --- 池标签 --------------------------------------------------------------
 * 旧实现写死 'Ntfs'。好处是"看起来像系统组件"，坏处是：**所有人拿到的都是
 * 同一个 tag** —— 只要 ACC 侧把"tag == 'Ntfs' 且不属于任何已加载模块"记成一条
 * 规则，就能一次性覆盖全部客户机。这属于"我们主动交出去的指纹"。
 *
 * 现在改成每次加载从一组系统也常用的标签里挑一个（时间派生，不引入随机数 API）：
 * 单次仍像系统分配，跨机器/跨重启不再一致 -> 签名失效，误报率也不升高。
 * 注意：值在整个加载周期内固定（ExFreePoolWithTag 必须和分配时一致）。
 * ---------------------------------------------------------------------- */
/* 注意：这里必须用原始类型名 */
extern unsigned int g_cs2_pool_tag;
unsigned int cs2_pool_tag_init(void);
#define CS2_POOL_TAG   (g_cs2_pool_tag ? g_cs2_pool_tag : cs2_pool_tag_init())
static __forceinline PVOID cs2_alloc_np(SIZE_T size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, CS2_POOL_TAG);
}

/* Wipe before free. A freed pool block is handed to the next caller with its old
 * bytes intact, so anything we ever staged there stays readable: process names
 * (the enumeration buffer contains "cs2.exe" verbatim), copies of game module
 * pages (sigscan chunks), decrypted frame bodies. Callers that know the length
 * pass it; pass 0 only when the buffer never held sensitive content. */
static __forceinline void cs2_free_wipe(PVOID p, SIZE_T n)
{
    if (p == NULL) return;
    if (n != 0) RtlZeroMemory(p, n);
    ExFreePoolWithTag(p, CS2_POOL_TAG);
}

/* --- 进程/模块名 (全部走加解密字符串表, 见 cs2_strs.h) ---------------------
 * 反作弊最便宜的检测是"扫 binary 里的字符串": 一个"来历不明"的驱动镜像里
 * 出现 "cs2.exe" / "client.dll" 就是一条几乎可以定罪的签名。
 * 所以这些明文一律不落在 .sys 里, 改由 tools\gen-strs.ps1 生成密文表,
 * 运行期解到内核栈/静态缓冲:
 *    cs2_s_exe_a()   -> "cs2.exe"  (ANSI)
 *    cs2_s_exe_w()   -> "cs2.exe"  (WCHAR, 给 ZwQuerySystemInformation 比对)
 *    cs2_s_client()  -> "client.dll"
 *    cs2_s_engine()  -> "engine2.dll"
 *    cs2_s_schema()  -> "schemasystem.dll"
 * ------------------------------------------------------------------------- */

/* --- 鍩烘湰绫诲瀷鍒悕, 璁╀唬鐮佺煭涓€鐐?---------------------------------------- */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed int         i32;
typedef signed long long   i64;
typedef float              f32;

#define CS2_OK            1
#define CS2_FAIL          0

/* --- 鍐呭瓨璇诲啓鍘熻 (kmem.c) ---------------------------------------------- */
typedef struct cs2_target {
    PEPROCESS process;       /* 宸插紩鐢? 鐢ㄥ畬 ObDereferenceObject */
    u32       pid;
    u64       base;          /* 涓绘ā鍧楀熀鍧€ */
    u64       size;          /* 涓绘ā鍧楁槧鍍忓ぇ灏?*/
} cs2_target;

NTSTATUS cs2_target_open(cs2_target* t);
void     cs2_target_close(cs2_target* t);
int      cs2_target_alive(cs2_target* t);

/* 浠庣洰鏍囪繘绋嬭 len 瀛楄妭鍒版湰鍦扮紦鍐层€傝繑鍥?CS2_OK/CS2_FAIL銆? * 鍐呴儴浼樺厛 MmCopyVirtualMemory(鏃犲彞鏌?, 澶辫触鏃堕€€鍒版墜鍔ㄩ〉琛ㄩ亶鍘嗐€?*/
int  cs2_read(cs2_target* t, u64 va, void* dst, u32 len);

/* 璇绘寚閽?+ 鍩烘湰鍙鎬у垽鏂?*/
int  cs2_read_ptr(cs2_target* t, u64 va, u64* out);

/* 鍙栨ā鍧椾俊鎭?*/
typedef struct cs2_module {
    u64  base;              /* 鏄犲儚鍩哄潃 */
    u32  size;
    u64  entry;
    u32  timestamp;
    u16  sections;          /* 鑺傛暟閲?*/
} cs2_module;

int  cs2_module_find(cs2_target* t, const char* name, cs2_module* out);

/* 璇?C 瀛楃涓?(鐩爣杩涚▼鍐?, 鎴柇鍒?dstLen-1, 淇濊瘉 NUL 缁撳熬 */
int  cs2_read_cstr(cs2_target* t, u64 va, char* dst, u32 dstLen);

/* --- 鍦板潃绌洪棿鍩烘湰璁块棶 (缁欐壂鎻忓櫒鐢? 璧板綋鍓嶈繘绋嬮〉琛? --------------------- */
/* 鍥犱负鎴戜滑瑕佹壂鐨勬槸 client.dll 鐨勬槧鍍?宸插湪鐩爣杩涚▼鍦板潃绌洪棿閲?, 鎵弿鏃? * 闇€瑕佷互鐩爣杩涚▼鐨勫湴鍧€绌洪棿涓轰笂涓嬫枃銆傜敤 KeStackAttachProcess 鍒囨崲銆?*/

#endif /* CS2CORE_KM_H */
