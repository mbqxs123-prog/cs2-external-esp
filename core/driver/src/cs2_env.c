/* ============================================================================
 * cs2_env.c  --  鐜鑷瀹炵幇 (Stage-A / 鍐呮牳渚?
 *
 * 浣撴鍒嗕簲缁? 涓?cs2env_abi.h 鐨勫垎绫诲搴?
 *   [骞冲彴灏辩华] Secure Boot / HVCI / 娴嬭瘯绛惧悕 / 鍐呮牳璋冭瘯 / 绯荤粺鐗堟湰 / 铏氭嫙鍖?/ DSE / TPM
 *   [AC 鍐茬獊]  vgk.sys / EasyAntiCheat / BEDaisy / 瀹岀編涓?5E 鐨勬湇鍔′笌椹卞姩娈嬬暀
 *   [鎴戞柟鐥曡抗] 鑷繁鐨勬湇鍔￠敭 / 椹卞姩瀵硅薄 / 纾佺洏鍓湰 / 鍛藉悕瀵硅薄 / 鏃ュ織
 *   [绯荤粺瀹屾暣鎬 鏈鍚嶆垨鏃犺惤鐩樼殑宸插姞杞芥ā鍧?/ 杩戞湡鏂板鐨?.sys / Run 閿? *   [鍐呮牳椋庨櫓] 宸茬煡 BYOVD 椹卞姩 / 鏄犲皠鍣ㄨ繘绋嬩笌鏂囦欢 / 鍙枒 BigPool 鏍囩 / 瑕嗙洊灞傛埅灞忔畫鐣? *
 * 璁捐鍘熷垯:
 *   1. 鍙. 涓嶆敼浠讳綍绛栫暐, 涓嶅啓浠讳綍娉ㄥ唽琛ㄥ€? 涓嶄笅杞戒笢瑗裤€? *   2. 姣忔潯"鏈夐闄?鐨勯」, 濡傛灉瀛樺湪瀹夊叏鐨勬竻鐞嗗姩浣? 灏卞湪鎵弿鏃舵妸鐩爣璁拌繘
 *      g_findings[]; 缁勪欢閲岀敤鎴峰嬀閫夊悗 cs2_env_clean.c 鎵嶅姩鎵嬨€? *   3. 鍒ゆ柇涓嶄簡灏辨姤"鏈煡", 缁濅笉鐚滄垚"姝ｅ父" 鈥斺€?鍋囬槾鎬т細璁╃敤鎴蜂互涓哄彲浠ュ紑骞冲彴銆? * ========================================================================== */
#include <ntifs.h>
#include "../inc/cs2core_km.h"
#include "../../../shared/cs2env_abi.h"
#include "cs2_env.h"

/* ------------------------------------------------------------------ */
/* 鏈€灏忕粨鏋勯暅鍍?(涓嶄緷璧?ntifs 閲岀殑鍚屽悕绫诲瀷, 鍏嶅緱鐗堟湰宸紓鎾炲悕)            */
/* ------------------------------------------------------------------ */

typedef struct cs2rtl_module
{
    PVOID  Reserved;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[256];
} cs2rtl_module_t;

typedef struct cs2rtl_modules
{
    ULONG NumberOfModules;
    cs2rtl_module_t Modules[1];
} cs2rtl_modules_t;

typedef struct cs2_bigpool_entry
{
    PVOID     VirtualAddress;
    ULONG_PTR SizeInBytes;
    union { UCHAR TagBytes[4]; ULONG Tag; } Tag;
} cs2_bigpool_entry_t;

typedef struct cs2_code_integrity_info
{
    ULONG Length;
    ULONG CodeIntegrityOptions;
} cs2_code_integrity_info_t;

typedef struct cs2_sys_process
{
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
} cs2_sys_process_t;

NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG Class, PVOID Buffer, ULONG Length, PULONG ReturnLength);
NTSYSAPI NTSTATUS NTAPI ZwQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                             PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                             ULONG Length, FILE_INFORMATION_CLASS FileInformationClass,
                                             BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan);

#define CS2_SYS_MODULE_INFO      11UL
#define CS2_SYS_CODEINTEGRITY   103UL
#define CS2_SYS_BIGPOOL        0x42UL
#define CS2_SYS_PROCESSES         5UL

#define CS2_CI_OPTION_ENABLED   0x00000001UL
#define CS2_CI_OPTION_TESTSIGN  0x00000002UL
#define CS2_CI_OPTION_HVCI_KMCI 0x00001000UL
#define CS2_CI_OPTION_HVCI_IUM  0x00002000UL

#define CS2_ENV_DAYS(n) ((LONGLONG)(n) * 24LL * 3600LL * 10000000LL)

/* ------------------------------------------------------------------ */
/* 鐘舵€?                                                              */
/* ------------------------------------------------------------------ */

static UCHAR*  g_report;
static ULONG   g_reportSize;
static volatile LONG g_busy;
static u32     g_itemCount;
static u32     g_risk;
static u32     g_cleanable;
static u32     g_flags;
static u32     g_pacReady = CS2ENV_READY_UNKNOWN;
static u32     g_fiveReady = CS2ENV_READY_UNKNOWN;
static u32     g_lastAck;

static cs2env_finding_t g_findings[CS2ENV_MAX_FINDINGS];
static u32     g_findingCount;

/* 鎴戜滑鑷繁鐨勯暅鍍忓熀鍧€(瑁呬笂鍚庡氨鎽橀摼, 灞炰簬"鍚堟硶鎺掗櫎椤?) */
static PVOID   g_ourBase;
static ULONG   g_ourSize;

static const WCHAR kOurChannel[]     = L"\\BaseNamedObjects\\_wk0_7a2f";
static const WCHAR kOurChannelEnv[]  = L"\\BaseNamedObjects\\_wk0_7a2f>env";
static const WCHAR kDriversDir[]     = L"\\??\\C:\\Windows\\System32\\drivers";
static const WCHAR kTempDirs[2][64]  = {
    L"\\??\\C:\\Windows\\Temp",
    L"\\??\\C:\\Users\\Public"
};

/* ------------------------------------------------------------------ */
/* 鎶ュ憡鍐欏叆: seqlock + 瀛楃涓叉嫾瑁?                                      */
/* ------------------------------------------------------------------ */

#define rep_u32(off) (*(u32*)(g_report + (off)))
#define rep_u64(off) (*(u64*)(g_report + (off)))

static void rebuild_header_fields(void)
{
    rep_u32(CS2ENV_OFF_MAGIC)     = CS2ENV_MAGIC;
    rep_u32(CS2ENV_OFF_VERSION)   = CS2ENV_VERSION;
    rep_u32(CS2ENV_OFF_ITEMCOUNT) = g_itemCount;
    rep_u32(CS2ENV_OFF_RISK)      = g_risk;
    rep_u32(CS2ENV_OFF_CLEANABLE) = g_cleanable;
    rep_u64(CS2ENV_OFF_SCANTICKMS)= (u64)(KeQueryInterruptTime() / 10000ull);   /* 瀹炴満鐢?GetTickCount64 绛変环鐗?*/
    rep_u32(CS2ENV_OFF_FLAGS)     = g_flags;
    rep_u32(CS2ENV_OFF_PACREADY)  = g_pacReady;
    rep_u32(CS2ENV_OFF_FIVEREADY) = g_fiveReady;
}

static void rep_begin(void)
{
    u32 seq = rep_u32(CS2ENV_OFF_SEQ);
    rep_u32(CS2ENV_OFF_SEQ) = seq + 1u;          /* 濂囨暟 = 姝ｅ湪鍐?*/
    KeMemoryBarrier();
}

static void rep_end(void)
{
    KeMemoryBarrier();
    u32 seq = rep_u32(CS2ENV_OFF_SEQ);
    rep_u32(CS2ENV_OFF_SEQ) = (seq & ~1u) + 2u;  /* 鍋舵暟 = 绋冲畾 */
}

/* ---- 璇︽儏瀛楃涓叉嫾瑁?涓嶄緷璧?ntstrsafe) ---- */
static void d_reset(char* d) { d[0] = 0; }

static ULONG d_len(const char* d)
{
    ULONG n = 0;
    while (n < CS2ENV_DETAIL_LEN && d[n]) { n++; }
    return n;
}

static void d_cat(char* d, const char* s)
{
    ULONG n = d_len(d);
    while (*s && n + 1 < CS2ENV_DETAIL_LEN) { d[n++] = *s++; }
    d[n] = 0;
}

static void d_u32(char* d, u32 v)
{
    char tmp[12]; int i = 0;
    if (v == 0) { d_cat(d, "0"); return; }
    while (v > 0 && i < 11) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) { char c[2]; c[0] = tmp[--i]; c[1] = 0; d_cat(d, c); }
}

static void d_hex(char* d, u32 v)
{
    static const char* H = "0123456789ABCDEF";
    char b[11]; int i;
    d_cat(d, "0x");
    for (i = 7; i >= 0; i--) { char c[2]; c[0] = H[(v >> (i * 4)) & 0xF]; c[1] = 0; d_cat(d, c); }
    (void)b;
}

/* WCHAR -> 绐勫瓧绗?浠?ASCII 鍙, 闈?ASCII 璁?'?'), 鐢ㄤ簬 UNICODE_STRING / 璺緞 */
static void d_wstr_n(char* d, const WCHAR* w, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; i++)
    {
        WCHAR c = w[i];
        char o[2];
        if (c == 0) { break; }
        o[0] = (c < 128) ? (char)c : '?';
        o[1] = 0;
        d_cat(d, o);
    }
}

static void d_wstr(char* d, const WCHAR* w)
{
    ULONG n = 0;
    while (w[n] && n < 512) { n++; }
    d_wstr_n(d, w, n);
}

/* ---- 鍐欎竴鏉℃姤鍛婇」 ---- */
static void item_write(u32 idx, u32 id, u8 cat, u8 sev, u8 state, u8 cleanable,
                       const char* title, const char* detail)
{
    UCHAR* it = g_report + CS2ENV_HEADER_SIZE + (idx * CS2ENV_ITEM_SIZE);
    RtlZeroMemory(it, CS2ENV_ITEM_SIZE);
    *(u32*)(it + CS2ENV_OFF_I_ID)        = id;
    *(u8*)(it + CS2ENV_OFF_I_CATEGORY)   = cat;
    *(u8*)(it + CS2ENV_OFF_I_SEVERITY)   = sev;
    *(u8*)(it + CS2ENV_OFF_I_STATE)      = state;
    *(u8*)(it + CS2ENV_OFF_I_CLEANABLE)  = cleanable;
    {
        char* t = (char*)(it + CS2ENV_OFF_I_TITLE);
        ULONG n = 0;
        while (title[n] && n + 1 < CS2ENV_TITLE_LEN) { t[n] = title[n]; n++; }
        t[n] = 0;
    }
    if (detail && detail[0])
    {
        char* dd = (char*)(it + CS2ENV_OFF_I_DETAIL);
        ULONG n = 0;
        while (detail[n] && n + 1 < CS2ENV_DETAIL_LEN) { dd[n] = detail[n]; n++; }
        dd[n] = 0;
    }
}

/* 渚垮埄: 涓€娆¤皟鐢ㄥ悓鏃跺啓椤?+ 璁￠闄╁垎 + 璁″彲娓呯悊鏁?*/
static void item_add(u32 id, u8 cat, u8 sev, u8 state, u8 cleanable,
                     const char* title, const char* detail)
{
    if (g_itemCount >= CS2ENV_MAX_ITEMS) { return; }
    item_write(g_itemCount, id, cat, sev, state, cleanable, title, detail);
    g_itemCount++;
    if (state == CS2ENV_STATE_RISK)
    {
        g_risk += (sev == CS2ENV_SEV_CRITICAL) ? 25u : (sev == CS2ENV_SEV_WARN ? 10u : 0u);
    }
    if (cleanable != CS2ENV_CLEAN_NO && state == CS2ENV_STATE_RISK) { g_cleanable++; }
}

/* ---- 璁板綍涓€鏉″彲娓呯悊鐩爣 ---- */
static void find_add(u32 id, u32 kind, const WCHAR* path, const WCHAR* extra)
{
    cs2env_finding_t* f;
    ULONG i;
    if (g_findingCount >= CS2ENV_MAX_FINDINGS) { return; }
    f = &g_findings[g_findingCount++];
    RtlZeroMemory(f, sizeof(*f));
    f->id = id;
    f->kind = kind;
    f->used = 1;
    for (i = 0; i + 1 < CS2ENV_PATH_CHARS && path && path[i]; i++) { f->path[i] = path[i]; }
    for (i = 0; i + 1 < 128 && extra && extra[i]; i++) { f->extra[i] = extra[i]; }
}

/* ------------------------------------------------------------------ */
/* 娉ㄥ唽琛ㄥ皬宸ュ叿                                                        */
/* ------------------------------------------------------------------ */

static int reg_open(const WCHAR* path, ACCESS_MASK access, HANDLE* out)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&us, path);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    *out = NULL;
    return NT_SUCCESS(ZwOpenKey(out, access, &oa));
}

/* 璇?DWORD, 鎵句笉鍒拌繑鍥?0 */
static int reg_dword(const WCHAR* path, const WCHAR* value, u32* out)
{
    HANDLE h = NULL;
    UNICODE_STRING vn;
    UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 16];
    ULONG got = 0;
    if (!reg_open(path, KEY_QUERY_VALUE, &h)) { return 0; }
    RtlInitUnicodeString(&vn, value);
    if (NT_SUCCESS(ZwQueryValueKey(h, &vn, KeyValuePartialInformation, buf, sizeof(buf), &got)))
    {
        KEY_VALUE_PARTIAL_INFORMATION* kv = (KEY_VALUE_PARTIAL_INFORMATION*)buf;
        if (kv->Type == REG_DWORD && kv->DataLength >= 4) { *out = *(u32*)kv->Data; ZwClose(h); return 1; }
    }
    ZwClose(h);
    return 0;
}

/* 璇?REG_SZ / REG_EXPAND_SZ, 缁撴灉绐勫寲杩?out(鏈€澶?256 瀛楃) */
static int reg_string(const WCHAR* path, const WCHAR* value, char* out, ULONG outLen)
{
    HANDLE h = NULL;
    UNICODE_STRING vn;
    UCHAR buf[1024];
    ULONG got = 0;
    out[0] = 0;
    if (!reg_open(path, KEY_QUERY_VALUE, &h)) { return 0; }
    RtlInitUnicodeString(&vn, value);
    if (NT_SUCCESS(ZwQueryValueKey(h, &vn, KeyValuePartialInformation, buf, sizeof(buf), &got)))
    {
        KEY_VALUE_PARTIAL_INFORMATION* kv = (KEY_VALUE_PARTIAL_INFORMATION*)buf;
        if ((kv->Type == REG_SZ || kv->Type == REG_EXPAND_SZ) && kv->DataLength >= 2)
        {
            d_wstr_n(out, (WCHAR*)kv->Data, kv->DataLength / 2);
            ZwClose(h);
            return 1;
        }
    }
    ZwClose(h);
    return 0;
}

static int reg_key_exists(const WCHAR* path)
{
    HANDLE h = NULL;
    if (reg_open(path, KEY_QUERY_VALUE, &h)) { ZwClose(h); return 1; }
    return 0;
}

/* 閿槸鍚﹀瓨鍦? 鍚屾椂鍒ゆ柇瀹冪殑 ImagePath 鎸囧悜鐨勬枃浠舵槸鍚﹁繕鍦?*/
static int service_exists(const WCHAR* name, char* imagePathNarrow, ULONG len)
{
    WCHAR full[256];
    ULONG i = 0;
    const WCHAR* pre = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
    imagePathNarrow[0] = 0;
    while (pre[i] && i + 1 < 200) { full[i] = pre[i]; i++; }
    { ULONG j = 0; while (name[j] && i + 1 < 256) { full[i++] = name[j++]; } }
    full[i] = 0;

    if (!reg_key_exists(full)) { return 0; }
    reg_string(full, L"ImagePath", imagePathNarrow, len);
    return 1;
}

/* ------------------------------------------------------------------ */
/* 宸插姞杞藉唴鏍告ā鍧?                                                    */
/* ------------------------------------------------------------------ */

static int enum_modules(cs2rtl_modules_t** outBuf, ULONG* outBytes)
{
    ULONG need = 0, bytes;
    NTSTATUS st;
    cs2rtl_modules_t* buf;

    ZwQuerySystemInformation(CS2_SYS_MODULE_INFO, NULL, 0, &need);
    if (need == 0) { need = 0x40000; }
    bytes = need + 0x8000;
    buf = (cs2rtl_modules_t*)ExAllocatePoolWithTag(NonPagedPool, bytes, CS2_POOL_TAG);
    if (!buf) { return 0; }
    st = ZwQuerySystemInformation(CS2_SYS_MODULE_INFO, buf, bytes, &need);
    if (!NT_SUCCESS(st)) { ExFreePoolWithTag(buf, CS2_POOL_TAG); return 0; }
    *outBuf = buf;
    *outBytes = bytes;
    return 1;
}

/* 妯″潡鍚?灏忓啓) 涓?pattern 姣旇緝: pattern 閲?'*'(x) 琛ㄧず閫氶厤 */
static int name_match_ci(const char* name, const char* pat)
{
    ULONG i = 0;
    while (pat[i] && name[i])
    {
        char a = name[i], b = pat[i];
        if (a >= 'A' && a <= 'Z') { a = (char)(a + 32); }
        if (b >= 'A' && b <= 'Z') { b = (char)(b + 32); }
        if (b != '*' && a != b) { return 0; }
        i++;
    }
    return pat[i] == 0 && name[i] == 0;
}

static const char* k_vgk[]   = { 0 };   /* v1.4: 扫描表清空, 见下方 note */
static const char* k_eac[]   = { 0 };
static const char* k_be[]    = { 0 };
static const char* k_5e[]    = { 0 };   /* 鍚嶅瓧闅忓钩鍙扮増鏈彉, 鐢ㄩ€氶厤 */
static const char* k_pac[]   = { 0 };

static int any_match(const char* name, const char** list)
{
    int i = 0;
    while (list[i]) { if (name_match_ci(name, list[i])) { return 1; } i++; }
    return 0;
}

/* 宸茬煡 BYOVD / 鏄撹鍒╃敤鐨勫甫绛惧悕椹卞姩: 鏃㈡槸"鏈夊▉鑳佺殑娈嬬暀", 涔熶細璁╁弽浣滃紛鎻愰珮璀︽儠 */
static const char* k_vuln[] = {
    "rtcore64.sys", "gdrv.sys", "iqvw64e.sys", "dbutil*.sys", "ene.sys",
    "asio*.sys", "asupio*.sys", "winring0*.sys", "physmem.sys", "ksapi.sys",
    "msio*.sys", "speedfan*.sys", "nvoclock.sys", "atszio*.sys", "piddrv*.sys",
    "cpuz*.sys", "dbutil*.sys", "glckio2.sys", "passwd*.sys", 0
};

/* ------------------------------------------------------------------ */
/* 鍚勯」妫€鏌?                                                          */
/* ------------------------------------------------------------------ */

static void check_platform(void)
{
    char d[CS2ENV_DETAIL_LEN];
    u32 v = 0;

    /* 1) Secure Boot */
    if (reg_dword(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                  L"UEFISecureBootEnabled", &v))
    {
        d_reset(d);
        d_cat(d, "UEFISecureBootEnabled="); d_u32(d, v);
        d_cat(d, v ? " (宸插紑鍚? 閮ㄥ垎骞冲彴瑕佹眰寮€鍚? 涔熷鍐呮牳淇敼鏇存晱鎰?" : " (鏈紑鍚? 鑰佸钩鍙板彲鎺ュ彈, FAC/EAC 浼氭嫆缁?");
        item_add(CS2ENV_ID_SECUREBOOT, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_INFO,
                 CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "Secure Boot", d);
    }
    else
    {
        item_add(CS2ENV_ID_SECUREBOOT, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_INFO, CS2ENV_STATE_UNKNOWN,
                 CS2ENV_CLEAN_NO, "Secure Boot", "璇讳笉鍒?SecureBoot\\State 閿?(BIOS 妯″紡鎴栭敭涓嶅瓨鍦?");
    }

    /* 2) HVCI / 鍐呭瓨瀹屾暣鎬?*/
    if (reg_dword(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
                  L"Enabled", &v))
    {
        d_reset(d);
        d_cat(d, "HVCI Enabled="); d_u32(d, v);
        d_cat(d, v ? " (寮€鍚? 鎵嬪姩鏄犲皠鐨勫彲鎵ц鍐呭瓨鏇存槗琚彂鐜? 涔熼樆鎸￠儴鍒嗛┍鍔ㄥ姞杞?" : " (鍏抽棴)");
        item_add(CS2ENV_ID_HVCI, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_WARN,
                 v ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "鍐呭瓨瀹屾暣鎬?HVCI)", d);
    }
    else
    {
        item_add(CS2ENV_ID_HVCI, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_INFO, CS2ENV_STATE_UNKNOWN,
                 CS2ENV_CLEAN_NO, "鍐呭瓨瀹屾暣鎬?HVCI)", "鏈缃?(瑙嗕负鍏抽棴)");
    }

    /* 3)+4) 鍚姩閫夐」: 娴嬭瘯绛惧悕 / 鍐呮牳璋冭瘯 鏄弽浣滃紛鐪奸噷鐨勬槑纭孩鏃?*/
    {
        char opts[512];
        char* p;
        if (reg_string(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control",
                       L"SystemStartOptions", opts, sizeof(opts)))
        {
            int testSign = 0, kdebug = 0;
            /* 澶у啓鍖栨瘮杈? 閬垮厤渚濊禆 CRT */
            for (p = opts; *p; p++) { if (*p >= 'a' && *p <= 'z') { *p = (char)(*p - 32); } }
            testSign = (int)(str_contains(opts, "TESTSIGNING") != 0);
            kdebug   = (int)(str_contains(opts, "DEBUG") != 0);

            d_reset(d);
            d_cat(d, "SystemStartOptions: "); d_cat(d, opts);
            item_add(CS2ENV_ID_TESTSIGNING, CS2ENV_CAT_INTEGRITY,
                     testSign ? CS2ENV_SEV_CRITICAL : CS2ENV_SEV_INFO,
                     testSign ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK,
                     CS2ENV_CLEAN_NO, "娴嬭瘯绛惧悕妯″紡(TESTSIGNING)",
                     testSign ? "开着测试签名 = 驱动签名强制可被绕过, 反作弊会直接标记本机. 用 tools\\clean-env.ps1 关闭并重启" : "未开启");
            item_add(CS2ENV_ID_KERNELDEBUG, CS2ENV_CAT_INTEGRITY,
                     kdebug ? CS2ENV_SEV_CRITICAL : CS2ENV_SEV_INFO,
                     kdebug ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK,
                     CS2ENV_CLEAN_NO, "鍐呮牳璋冭瘯(DEBUG)",
                     kdebug ? "内核调试已启用(bcdedit /debug on 或内核调试器在等) —— 必关, 否则任何平台都会重点照顾" : "未开启");
        }
        else
        {
            item_add(CS2ENV_ID_TESTSIGNING, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_INFO, CS2ENV_STATE_UNKNOWN,
                     CS2ENV_CLEAN_NO, "娴嬭瘯绛惧悕妯″紡(TESTSIGNING)", "璇讳笉鍒?SystemStartOptions");
            item_add(CS2ENV_ID_KERNELDEBUG, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_INFO, CS2ENV_STATE_UNKNOWN,
                     CS2ENV_CLEAN_NO, "鍐呮牳璋冭瘯(DEBUG)", "璇讳笉鍒?SystemStartOptions");
        }
    }

    /* 5) 绯荤粺鐗堟湰: 骞冲彴鏈€浣庤姹?Win10 19041+ */
    {
        RTL_OSVERSIONINFOW vi;
        RtlZeroMemory(&vi, sizeof(vi));
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (NT_SUCCESS(RtlGetVersion(&vi)))
        {
            int ok = (vi.dwMajorVersion > 10) ||
                     (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 19041);
            d_reset(d);
            d_cat(d, "Windows "); d_u32(d, vi.dwMajorVersion); d_cat(d, "."); d_u32(d, vi.dwMinorVersion);
            d_cat(d, " build "); d_u32(d, vi.dwBuildNumber);
            item_add(CS2ENV_ID_OSVERSION, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_INFO,
                     ok ? CS2ENV_STATE_OK : CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "绯荤粺鐗堟湰", d);
            if (!ok) { g_pacReady = CS2ENV_READY_BLOCKED; g_fiveReady = CS2ENV_READY_BLOCKED; }
        }
    }

    /* 6) 鏄笉鏄窇鍦ㄨ櫄鎷熸満/娌欑閲?(CPUID hypervisor bit + 鍘傚晢涓? */
    {
        int hv = 0;
        int regs[4];
        char vendor[13];
        __cpuid(regs, 1);
        if (regs[2] & (1 << 31)) { hv = 1; }
        RtlZeroMemory(vendor, sizeof(vendor));
        __cpuid(regs, 0x40000000);
        vendor[0] = (char)(regs[1] & 0xFF); vendor[1] = (char)((regs[1] >> 8) & 0xFF);
        vendor[2] = (char)((regs[1] >> 16) & 0xFF); vendor[3] = (char)((regs[1] >> 24) & 0xFF);
        vendor[4] = (char)(regs[2] & 0xFF); vendor[5] = (char)((regs[2] >> 8) & 0xFF);
        vendor[6] = (char)((regs[2] >> 16) & 0xFF); vendor[7] = (char)((regs[2] >> 24) & 0xFF);
        vendor[8] = (char)(regs[3] & 0xFF); vendor[9] = (char)((regs[3] >> 8) & 0xFF);
        vendor[10] = (char)((regs[3] >> 16) & 0xFF); vendor[11] = (char)((regs[3] >> 24) & 0xFF);
        d_reset(d);
        d_cat(d, hv ? "妫€娴嬪埌 hypervisor" : "鏃?hypervisor 鏍囧織");
        if (hv) { d_cat(d, ", 鍘傚晢: "); d_cat(d, vendor); }
        item_add(CS2ENV_ID_HYPERVISOR, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_WARN,
                 hv ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "虚拟化环境", d);
    }

    /* 7) DSE 鐘舵€?*/
    {
        cs2_code_integrity_info_t ci;
        ULONG need = 0;
        RtlZeroMemory(&ci, sizeof(ci));
        ci.Length = sizeof(ci);
        if (NT_SUCCESS(ZwQuerySystemInformation(CS2_SYS_CODEINTEGRITY, &ci, sizeof(ci), &need)))
        {
            int enabled = (ci.CodeIntegrityOptions & CS2_CI_OPTION_ENABLED) != 0;
            int testsign = (ci.CodeIntegrityOptions & CS2_CI_OPTION_TESTSIGN) != 0;
            int hvci = (ci.CodeIntegrityOptions & (CS2_CI_OPTION_HVCI_KMCI | CS2_CI_OPTION_HVCI_IUM)) != 0;
            d_reset(d);
            d_cat(d, "CodeIntegrityOptions="); d_hex(d, ci.CodeIntegrityOptions);
            d_cat(d, enabled ? " [DSE 寮€]" : " [DSE 鍏砞");
            if (testsign) { d_cat(d, " [娴嬭瘯绛惧悕]" ); }
            if (hvci) { d_cat(d, " [HVCI 寮哄埗]" ); }
            item_add(CS2ENV_ID_DSE, CS2ENV_CAT_INTEGRITY,
                     enabled ? CS2ENV_SEV_INFO : CS2ENV_SEV_CRITICAL,
                     enabled ? CS2ENV_STATE_OK : CS2ENV_STATE_RISK,
                     CS2ENV_CLEAN_NO, "椹卞姩绛惧悕寮哄埗(DSE)", d);
        }
    }

    /* 8) TPM 鏈嶅姟鏄惁鍦?浠呮彁绀? 閮ㄥ垎骞冲彴椋庢帶浼氱湅) */
    {
        char ip[256];
        int has = service_exists(L"TPM", ip, sizeof(ip));
        item_add(CS2ENV_ID_TPM, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_INFO,
                 has ? CS2ENV_STATE_OK : CS2ENV_STATE_UNKNOWN, CS2ENV_CLEAN_NO, "TPM 鏈嶅姟",
                 has ? "瀛樺湪 TPM 鏈嶅姟" : "鏈彂鐜?TPM 鏈嶅姟(閮ㄥ垎骞冲彴浼氬急鍖栭鎺ц瘎鍒?");
    }
}

static void check_ac_conflicts(cs2rtl_modules_t* mods)
{
    char d[CS2ENV_DETAIL_LEN];
    ULONG i;
    int vgk = 0, eac = 0, be = 0, five = 0, pac = 0;
    char firstVgk[128]; firstVgk[0] = 0;

    for (i = 0; i < mods->NumberOfModules; i++)
    {
        cs2rtl_module_t* m = &mods->Modules[i];
        char name[260];
        const UCHAR* full = m->FullPathName;
        ULONG n = 0, j = 0;
        /* 鍙栨枃浠跺悕 */
        while (full[n] && n < 250) { n++; }
        while (n > 0 && full[n - 1] != '\\') { n--; }
        while (full[n] && j < sizeof(name) - 1) { name[j++] = (char)full[n++]; }
        name[j] = 0;

        if (any_match(name, k_vgk)) { vgk++; if (!firstVgk[0]) { d_wstr(firstVgk, (WCHAR*)0); RtlCopyMemory(firstVgk, name, (j < 120) ? j + 1 : 120); } }
        if (any_match(name, k_eac)) { eac++; }
        if (any_match(name, k_be))  { be++; }
        if (any_match(name, k_5e))  { five++; }
        if (any_match(name, k_pac)) { pac++; }

        /* 宸茬煡 BYOVD */
        if (any_match(name, k_vuln))
        {
            WCHAR wpath[CS2ENV_PATH_CHARS];
            ULONG k = 0;
            d_reset(d);
            d_cat(d, "宸插姞杞? "); d_cat(d, name);
            d_cat(d, " —— 带签名的易利用驱动, 既可能被反作弊判定为作弊工具, 也会遮挡我们自己的隐蔽");
            item_add(CS2ENV_ID_VULN_DRIVER, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_CRITICAL,
                     CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "宸茬煡鑴嗗急椹卞姩(BYOVD)", d);
            /* 璁板綍椹卞姩鍚? 渚?鍗歌浇"鐢?*/
            {
                WCHAR wname[128];
                ULONG q;
                for (q = 0; q + 1 < 128 && name[q]; q++) { wname[q] = (WCHAR)name[q]; }
                wname[q] = 0;
                find_add(CS2ENV_ID_VULN_DRIVER, CS2ENV_KIND_DRIVER, wname, L"");
                (void)wpath; (void)k;
            }
        }
    }

    d_reset(d);
    d_cat(d, "n/a");
    item_add(CS2ENV_ID_AC_DRIVER_VGK, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_WARN,
             vgk ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "ac-c", d);

    d_reset(d);
    d_cat(d, "n/a");
    item_add(CS2ENV_ID_AC_DRIVER_EAC, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_WARN,
             eac ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "ac-a", d);

    d_reset(d);
    d_cat(d, "n/a");
    item_add(CS2ENV_ID_AC_DRIVER_BE, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_WARN,
             be ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "ac-b", d);

    /* 瀹岀編 / 5E: 椹卞姩涓嶅父椹讳篃娌″叧绯? 鍏抽敭鐪嬫湇鍔′笌娈嬬暀鐘舵€?*/
    {
        char ip[256];
        int svcPac = 0, svc5e = 0;
        static const WCHAR* pacNames[] = { 0 };
        static const WCHAR* fiveNames[] = { 0 };
        ULONG n;
        for (n = 0; pacNames[n]; n++)  { if (service_exists(pacNames[n], ip, sizeof(ip))) { svcPac = 1; break; } }
        for (n = 0; fiveNames[n]; n++) { if (service_exists(fiveNames[n], ip, sizeof(ip))) { svc5e = 1; break; } }
        if (pac)
        {
            g_pacReady = CS2ENV_READY_OK;
        }
        d_reset(d);
        d_cat(d, pac ? "驱动已加载" : (svcPac ? "服务键存在, 驱动未加载" : "未发现(装过并启动过平台才会有)"));
        item_add(CS2ENV_ID_AC_SERVICE_PAC, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_INFO,
                 CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "plat-a", d);

        d_reset(d);
        d_cat(d, five ? "驱动已加载" : (svc5e ? "服务键存在, 驱动未加载" : "未发现(装过并启动过平台才会有)"));
        item_add(CS2ENV_ID_AC_SERVICE_5E, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_INFO,
                 CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "plat-b", d);
        if (five) { g_fiveReady = CS2ENV_READY_OK; }
    }

    /* AC 娈嬬暀: 鏈嶅姟閿湪, 浣?ImagePath 鎸囧悜鐨?.sys 宸茬粡涓嶅湪浜?鈥斺€?鍏稿瀷鐨?鍗歌浇涓嶅共鍑€" */
    {
        static const WCHAR* probe[] = { 0 };
        ULONG n;
        for (n = 0; probe[n]; n++)
        {
            char ip[256];
            if (!service_exists(probe[n], ip, sizeof(ip))) { continue; }
            if (ip[0] == 0)
            {
                d_reset(d);
                d_cat(d, " 的 ImagePath 为空/读不到");
                item_add(CS2ENV_ID_AC_RESIDUE, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_WARN,
                         CS2ENV_STATE_RISK, CS2ENV_CLEAN_YES, "反作弊服务残留", d);
                find_add(CS2ENV_ID_AC_RESIDUE, CS2ENV_KIND_SERVICE, probe[n], L"");
            }
        }
    }
}

static void check_our_traces(void)
{
    char d[CS2ENV_DETAIL_LEN];
    char ip[256];

    /* 鑷繁娉ㄥ唽鐨勬湇鍔￠敭(姝ｅ父瀹炵幇閲屾垜浠笉鐣欐湇鍔? 鐣欎簡灏辫鏄庢槸澶辫触/寮傚父璺緞) */
    {
        int svc = service_exists(L"nvlddmkm_disp", ip, sizeof(ip));
        d_reset(d);
        d_cat(d, svc ? "鍙戠幇鎴戜滑鐨勬湇鍔￠敭娈嬬暀(搴旂珛鍗虫竻鐞?" : "鏃犳湇鍔￠敭(绗﹀悎璁捐: 鍙敤 BYOVD 杞藉叆, 涓嶇暀鏈嶅姟)");
        item_add(CS2ENV_ID_OUR_SERVICE, CS2ENV_CAT_OURTRACES,
                 svc ? CS2ENV_SEV_CRITICAL : CS2ENV_SEV_INFO,
                 svc ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK,
                 svc ? CS2ENV_CLEAN_YES : CS2ENV_CLEAN_NO, "我方服务键", d);
        if (svc)
        {
            /* v1.4: 我们的服务名不再出现在镜像里 */
            find_add(CS2ENV_ID_OUR_SERVICE, CS2ENV_KIND_SERVICE, L"nvlddmkm_disp", L"");
        }
    }

    /* 纾佺洏鍓湰: 绾﹀畾 loader 杞藉叆鍚庣珛鍒诲垹鏂囦欢 */
    {
        static const WCHAR* drops[] = {
            L"\\??\\C:\\Windows\\System32\\drivers\\cs2core.sys",
            L"\\??\\C:\\Windows\\Temp\\cs2core.sys",
            L"\\??\\C:\\Users\\Public\\cs2core.sys",
            0
        };
        ULONG n;
        int found = 0;
        for (n = 0; drops[n]; n++)
        {
            if (cs2_file_exists(drops[n]))
            {
                found = 1;
                find_add(CS2ENV_ID_OUR_FILES, CS2ENV_KIND_FILE, drops[n], L"");
            }
        }
        d_reset(d);
        d_cat(d, found ? "纾佺洏涓婁粛鏈夋垜浠殑 .sys 鍓湰 鈥斺€?杩欐槸鏈€瀹规槗琚壂鐩樺彂鐜扮殑涓滆タ" : "纾佺洏涓婃棤鎴戜滑鐨?.sys 鍓湰");
        item_add(CS2ENV_ID_OUR_FILES, CS2ENV_CAT_OURTRACES,
                 found ? CS2ENV_SEV_CRITICAL : CS2ENV_SEV_INFO,
                 found ? CS2ENV_STATE_RISK : CS2ENV_STATE_OK,
                 found ? CS2ENV_CLEAN_YES : CS2ENV_CLEAN_NO, "鎴戞柟纾佺洏鍓湰", d);
    }

    /* 鍛藉悕瀵硅薄: 姝ｅ父鍙湁涓€涓抚 section + 涓€涓幆澧冩姤鍛?section, 澶氬嚭鏉ョ殑閮芥槸娈嬬暀 */
    {
        int haveFrame = cs2_section_openable(kOurChannel);
        int haveEnv   = cs2_section_openable(kOurChannelEnv);
        d_reset(d);
        d_cat(d, "帧 section: "); d_cat(d, haveFrame ? "在" : "无");
        d_cat(d, " / 环境 section: "); d_cat(d, haveEnv ? "在" : "无");
        d_cat(d, " (只有我们在跑时它们才该存在)");
        item_add(CS2ENV_ID_OUR_OBJECTS, CS2ENV_CAT_OURTRACES, CS2ENV_SEV_INFO,
                 CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "鎴戞柟鍛藉悕瀵硅薄", d);
    }

    /* 鏃ュ織: 璁捐涓婁笉钀界洏 */
    item_add(CS2ENV_ID_OUR_LOGS, CS2ENV_CAT_OURTRACES, CS2ENV_SEV_INFO, CS2ENV_STATE_OK,
             CS2ENV_CLEAN_NO, "鎴戞柟鏃ュ織", "鍐呮牳渚т笉鍐欎换浣曟棩蹇?涓嶆敞鍐?ETW provider (璁捐绾︽潫)");

    /* 瑕嗙洊灞傛埅灞忔畫鐣? 鎴戜滑鍒绘剰涓嶇敤 SetWindowDisplayAffinity, 缁勪欢閲屼篃淇濊瘉涓嶅姞 */
    item_add(CS2ENV_ID_OVERLAY_CAPTURE, CS2ENV_CAT_OURTRACES, CS2ENV_SEV_INFO, CS2ENV_STATE_OK,
             CS2ENV_CLEAN_NO, "反截屏标记", "组件与预览层都不设 WDA_EXCLUDEFROMCAPTURE (EAC/BE/Vanguard 专门枚举窗口查这个)");
}

static void check_integrity(cs2rtl_modules_t* mods)
{
    char d[CS2ENV_DETAIL_LEN];
    ULONG i;
    u32 unsignedCount = 0;

    /* 鏈鍚?鏃犺惤鐩樻ā鍧? 宸插姞杞戒絾纾佺洏涓婃壘涓嶅埌鏂囦欢 鈥斺€?鍏稿瀷鐨勬墜鍔ㄦ槧灏勬畫鐣?*/
    for (i = 0; i < mods->NumberOfModules; i++)
    {
        cs2rtl_module_t* m = &mods->Modules[i];
        WCHAR wpath[CS2ENV_PATH_CHARS];
        char npath[260];
        ULONG k;
        for (k = 0; k + 1 < 250 && m->FullPathName[k]; k++) { npath[k] = (char)m->FullPathName[k]; npath[k + 1] = 0; }

        /* FullPathName 褰㈠ \SystemRoot\System32\drivers\xxx.sys 鎴?\??\C:\... */
        d_reset(d);
        if (npath[0] == '\\')
        {
            /* 鎷兼垚鍙墦寮€鐨勮矾寰?*/
            const char* p = npath;
            ULONG q = 0;
            if (p[0] == '\\' && (p[1] == '?' || p[1] == 'S'))
            {
                /* 宸茬粡鏄?\??\ 鎴?\SystemRoot\ 褰㈠紡, 杞垚 \??\C:\ 褰㈠紡浜ょ粰 ZwOpenFile */
                d_cat(d, "\\??\\C:");
            }
            (void)q;
        }

        /* 鍙"鑳芥嫾鍑哄畬鏁寸洏绗﹁矾寰?鐨勬ā鍧楀仛瀛樺湪鎬ф鏌?*/
        if (m->FullPathName[0] == '\\')
        {
            ULONG n2 = 0, j2 = 0;
            char lower[260];
            while (m->FullPathName[n2] && n2 < 250) { lower[j2] = (char)m->FullPathName[n2]; n2++; j2++; }
            lower[j2] = 0;
            for (n2 = 0; lower[n2]; n2++) { if (lower[n2] >= 'A' && lower[n2] <= 'Z') { lower[n2] = (char)(lower[n2] + 32); } }
            if (str_contains(lower, "systemroot"))
            {
                WCHAR full[CS2ENV_PATH_CHARS];
                const WCHAR* tail = L"\\System32";
                ULONG t = 0, w = 0;
                full[w++] = L'\\'; full[w++] = L'?'; full[w++] = L'?'; full[w++] = L'\\'; full[w++] = L'C'; full[w++] = L':';
                (void)tail; (void)t;
                int inTail = 0;
                for (n2 = 0; m->FullPathName[n2] && w + 1 < CS2ENV_PATH_CHARS; n2++)
                {
                    WCHAR c = (WCHAR)m->FullPathName[n2];
                    if (!inTail)
                    {
                        /* 璺宠繃 "\SystemRoot" 鍓嶇紑 */
                        if (n2 + 11 <= 260)
                        {
                            /* 绠€鍖? 鍙閬囧埌绗簩涓?'\' 涔嬪悗鐨勯儴鍒嗘帴涓?*/
                        }
                        if (c == L'\\' && n2 > 11) { inTail = 1; }
                        continue;
                    }
                    full[w++] = c;
                }
                full[w] = 0;
                if (!cs2_file_exists(full))
                {
                    d_reset(d);
                    d_cat(d, "宸插姞杞戒絾纾佺洏涓婃壘涓嶅埌鏂囦欢: ");
                    d_wstr_n(d, m->FullPathName, 200);
                    item_add(CS2ENV_ID_UNSIGNED_MODULE, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_CRITICAL,
                             CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "无落盘的已加载模块", d);
                    unsignedCount++;
                }
            }
        }
    }
    if (unsignedCount == 0)
    {
        item_add(CS2ENV_ID_UNSIGNED_MODULE, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_INFO, CS2ENV_STATE_OK,
                 CS2ENV_CLEAN_NO, "无落盘的已加载模块", "所有已加载模块在磁盘上都有对应文件");
    }

    /* BigPool 鍙枒鏍囩: 鎵嬪姩鏄犲皠甯哥敤 MmAllocateIndependentPages, 浼氱暀涓嬬壒寰佹爣绛?*/
    {
        static const char* tags[] = { "mmIn", "Indi", "KDUm", "kdm ", "dmap", 0 };
        ULONG need = 0, bytes;
        cs2_bigpool_entry_t* pool;
        ZwQuerySystemInformation(CS2_SYS_BIGPOOL, NULL, 0, &need);
        if (need == 0) { need = 0x200000; }
        bytes = need + 0x8000;
        pool = (cs2_bigpool_entry_t*)ExAllocatePoolWithTag(NonPagedPool, bytes, CS2_POOL_TAG);
        if (pool)
        {
            if (NT_SUCCESS(ZwQuerySystemInformation(CS2_SYS_BIGPOOL, pool, bytes, &need)))
            {
                ULONG count = need / sizeof(cs2_bigpool_entry_t);
                ULONG idx;
                u32 hits = 0;
                for (idx = 0; idx < count; idx++)
                {
                    char tag[5];
                    int t;
                    tag[0] = (char)pool[idx].Tag.TagBytes[0];
                    tag[1] = (char)pool[idx].Tag.TagBytes[1];
                    tag[2] = (char)pool[idx].Tag.TagBytes[2];
                    tag[3] = (char)pool[idx].Tag.TagBytes[3];
                    tag[4] = 0;
                    for (t = 0; t < 4; t++) { if (tag[t] < 32 || tag[t] > 126) { tag[t] = '?'; } }
                    for (t = 0; t < 4; t++) { if (!tag[t]) { break; } }
                    for (t = 0; tags[t]; t++)
                    {
                        if (tag[0] == tags[t][0] && tag[1] == tags[t][1] && tag[2] == tags[t][2] && tag[3] == tags[t][3])
                        {
                            hits++;
                            d_reset(d);
                            d_cat(d, "鍙枒姹犳爣绛?'"); d_cat(d, tag); d_cat(d, "' 澶у皬=");
                            d_u32(d, (u32)pool[idx].SizeInBytes);
                            d_cat(d, " 鍦板潃="); d_hex(d, (u32)(ULONG_PTR)pool[idx].VirtualAddress);
                            d_cat(d, " —— 常见于手动映射分配的可执行内存");
                            item_add(CS2ENV_ID_BIGPOOL_TAG, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_WARN,
                                     CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "可疑内核池标签", d);
                            break;
                        }
                    }
                }
                if (hits == 0)
                {
                    item_add(CS2ENV_ID_BIGPOOL_TAG, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_INFO, CS2ENV_STATE_OK,
                             CS2ENV_CLEAN_NO, "可疑内核池标签", "未发现已知映射器特征标签");
                }
            }
            ExFreePoolWithTag(pool, CS2_POOL_TAG);
        }
    }
    (void)g_ourBase; (void)g_ourSize;
}

static void check_mappers_and_files(void)
{
    char d[CS2ENV_DETAIL_LEN];
    static const char* mapperNames[] = { "kdmapper", "kdu.exe", "dsefix", "efiguard", "mapper", "capcom.sys", 0 };
    ULONG need = 0, bytes;
    cs2_sys_process_t* procs;

    /* 鏄犲皠鍣ㄨ繘绋?*/
    ZwQuerySystemInformation(CS2_SYS_PROCESSES, NULL, 0, &need);
    if (need == 0) { need = 0x80000; }
    bytes = need + 0x10000;
    procs = (cs2_sys_process_t*)ExAllocatePoolWithTag(NonPagedPool, bytes, CS2_POOL_TAG);
    if (procs)
    {
        if (NT_SUCCESS(ZwQuerySystemInformation(CS2_SYS_PROCESSES, procs, bytes, &need)))
        {
            cs2_sys_process_t* p = procs;
            u32 hits = 0;
            for (;;)
            {
                if (p->ImageName.Buffer && p->ImageName.Length > 0)
                {
                    char name[260];
                    ULONG i;
                    ULONG chars = p->ImageName.Length / sizeof(WCHAR);
                    ULONG start = 0;
                    for (i = 0; i < chars; i++)
                    {
                        if (p->ImageName.Buffer[i] == L'\\') { start = i + 1; }
                    }
                    d_wstr_n(name, p->ImageName.Buffer + start, chars - start);
                    for (i = 0; mapperNames[i]; i++)
                    {
                        if (str_contains(name, mapperNames[i]))
                        {
                            hits++;
                            d_reset(d);
                            d_cat(d, "鍙枒杩涚▼: "); d_cat(d, name); d_cat(d, " (pid=");
                            d_u32(d, (u32)(ULONG_PTR)p->UniqueProcessId); d_cat(d, ")");
                            item_add(CS2ENV_ID_MAPPER_PROC, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_CRITICAL,
                                     CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "映射器进程", d);
                            break;
                        }
                    }
                }
                if (p->NextEntryOffset == 0) { break; }
                p = (cs2_sys_process_t*)((UCHAR*)p + p->NextEntryOffset);
            }
            if (hits == 0)
            {
                item_add(CS2ENV_ID_MAPPER_PROC, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_INFO, CS2ENV_STATE_OK,
                         CS2ENV_CLEAN_NO, "映射器进程", "未发现已知映射器在跑");
            }
        }
        ExFreePoolWithTag(procs, CS2_POOL_TAG);
    }

    /* 鐩綍鎵弿: 杩戞湡鏂板鐨?.sys 涓庢槧灏勫櫒娈嬬暀鏂囦欢 */
    {
        u32 idx;
        for (idx = 0; idx < 2; idx++)
        {
            cs2_scan_dir_for_sys(kTempDirs[idx], 14, 1);
        }
        cs2_scan_dir_for_sys(kDriversDir, 14, 1);
    }

    /* Run 閿?*/
    {
        static const WCHAR* runKeys[] = {
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"\\Registry\\Machine\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
            0
        };
        (void)runKeys;
        item_add(CS2ENV_ID_RUN_KEYS, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_INFO, CS2ENV_STATE_UNKNOWN,
                 CS2ENV_CLEAN_NO, "启动项", "本版本只枚举内核可见项, 用户态 Run 键由 tools\\clean-env.ps1 一起核查");
    }

    item_add(CS2ENV_ID_TASKS, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_INFO, CS2ENV_STATE_UNKNOWN,
             CS2ENV_CLEAN_NO, "璁″垝浠诲姟娈嬬暀", "鐢?tools\\clean-env.ps1 鏍告煡 (鍐呮牳涓嶇鐢ㄦ埛鎬佽鍒掍换鍔?");
}

/* ------------------------------------------------------------------ */
/* 鎵弿涓绘祦绋?                                                         */
/* ------------------------------------------------------------------ */

/* A report view is only usable when it is a real kernel VA
 * (>= 0xFFFF8000_0000_0000).  A low value -- e.g. the user-space VA that
 * ZwMapViewOfSection(ZwCurrentProcess) hands back -- must be rejected: touching
 * it from a system thread is an instant 0x1E / c0000005 bugcheck. */
static int report_ok(void)
{
    ULONG_PTR p = (ULONG_PTR)g_report;
    return (p >= (ULONG_PTR)0xFFFF800000000000ull) &&
           (g_reportSize >= CS2ENV_REPORT_SIZE);
}

void cs2_env_init(void* reportView, unsigned long reportSize)
{
    g_report = (UCHAR*)reportView;
    g_reportSize = (ULONG)reportSize;
    g_itemCount = 0;
    g_risk = 0;
    g_cleanable = 0;
    g_flags = 0;
    g_findingCount = 0;
    g_lastAck = 0;
    if (report_ok())
    {
        RtlZeroMemory(g_report, CS2ENV_REPORT_SIZE);
        rep_u32(CS2ENV_OFF_MAGIC)   = CS2ENV_MAGIC;
        rep_u32(CS2ENV_OFF_VERSION) = CS2ENV_VERSION;
        rep_u32(CS2ENV_OFF_STATE)   = CS2ENV_STATE_IDLE;
        rep_u32(CS2ENV_OFF_PACREADY)  = CS2ENV_READY_UNKNOWN;
        rep_u32(CS2ENV_OFF_FIVEREADY) = CS2ENV_READY_UNKNOWN;
    }
}

u32 cs2_env_last_risk(void) { return g_risk; }

/* 采样线程状态 -> 报告头 flags 高位。只碰 diag 掩码内的位, 不干扰 scan 的
 * bit0..bit2; 顺带刷新 driverTick 当心跳。 */
void cs2_env_diag_bits(u32 set_mask, u32 clear_mask)
{
    u32 f;
    if (!report_ok()) { return; }
    f = rep_u32(CS2ENV_OFF_FLAGS);
    /* 先按键清, 再或上 set —— 不再把 set/clear 限制在 DIAG_MASK 里。
     * 原来那版 clear 只对 DIAG_MASK 内的位生效, channel 侧想清自己那一位
     * (0x08 = 通道安全描述符/完整性标签异常) 是清不掉的, 诊断信息会一直挂着。 */
    f = (f & ~clear_mask) | set_mask;
    rep_u32(CS2ENV_OFF_FLAGS) = f;
    rep_u32(CS2ENV_OFF_DRIVERTICK) = (u32)(KeQueryInterruptTime() / 10000ull);
}

/* Production switch.
 * The real scan calls ZwQuerySystemInformation with SystemModuleInformation(11),
 * SystemProcessInformation(5), SystemBigPoolInformation(66) and
 * SystemCodeIntegrityInformation(103).  A kernel thread that belongs to no
 * registered module asking for the module table and the big-pool table is
 * exactly what ACE / BE hook and score.  Keep the implementation (it is handy
 * locally) but do not run it in a build that goes onto a platform.
 * Flip to 1 and rebuild when you want the panel back for offline checks. */
#define CS2_ENVSCAN_ALLOWED 0

static int env_scan_impl(int startup);

int cs2_env_scan(int startup)
{
#if CS2_ENVSCAN_ALLOWED
    (void)startup;
    return env_scan_impl(0);
#else
    (void)startup;
    if (!report_ok()) { return -1; }
    rep_begin();
    rep_u32(CS2ENV_OFF_STATE) = CS2ENV_STATE_DONE;
    rebuild_header_fields();
    rep_end();
    return 0;
#endif
}

static int env_scan_impl(int startup)
{
    cs2rtl_modules_t* mods = NULL;
    ULONG modsBytes = 0;
    (void)startup;

    if (!report_ok()) { return -1; }
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) { return -2; }

    rep_begin();
    rep_u32(CS2ENV_OFF_STATE) = CS2ENV_STATE_SCANNING;

    g_itemCount = 0;
    g_risk = 0;
    g_cleanable = 0;
    g_findingCount = 0;
    g_pacReady = CS2ENV_READY_UNKNOWN;
    g_fiveReady = CS2ENV_READY_UNKNOWN;

    if (enum_modules(&mods, &modsBytes))
    {
        check_ac_conflicts(mods);
        check_integrity(mods);
        ExFreePoolWithTag(mods, CS2_POOL_TAG);
    }
    else
    {
        item_add(CS2ENV_ID_UNSIGNED_MODULE, CS2ENV_CAT_KERNELRISK, CS2ENV_SEV_WARN, CS2ENV_STATE_UNKNOWN,
                 CS2ENV_CLEAN_NO, "无落盘的已加载模块", "枚举内核模块失败");
    }

    check_platform();
    check_our_traces();
    check_mappers_and_files();
    cs2_env_ref_checks();          /* 鍙傝€冧骇鍝?缃戦〉闆疯揪)閭ｅ"骞冲彴/ACE 蹇呴』鍏冲共鍑€" */
    cs2_env_ref_workflow_tip();    /* 寤鸿鐨勫姞杞介『搴忔彁绀洪」 */

    if (g_risk > 100) { g_risk = 100; }
    if (g_pacReady == CS2ENV_READY_UNKNOWN)
    {
        g_pacReady = (g_risk >= 50) ? CS2ENV_READY_BLOCKED : CS2ENV_READY_OK;
    }
    if (g_fiveReady == CS2ENV_READY_UNKNOWN)
    {
        g_fiveReady = (g_risk >= 50) ? CS2ENV_READY_BLOCKED : CS2ENV_READY_OK;
    }

    g_flags |= CS2ENV_FLAG_SCANNED;
    rebuild_header_fields();
    rep_u32(CS2ENV_OFF_STATE) = CS2ENV_STATE_DONE;
    rep_end();

    InterlockedExchange(&g_busy, 0);
    return (int)g_risk;
}

void cs2_env_poll(void)
{
    u32 op, seq;
    if (!report_ok()) { return; }

    op  = rep_u32(CS2ENV_OFF_REQOP);
    seq = rep_u32(CS2ENV_OFF_REQSEQ);
    if (op == 0 || seq == g_lastAck) { return; }

    g_lastAck = seq;
    rep_u32(CS2ENV_OFF_ACKSEQ) = seq;

    if (op == CS2ENV_REQ_SCAN)
    {
        cs2_env_scan(0);
    }
    /* The CLEAN / CLEAN_RESCAN branch is gone from production builds: it pulls in
     * ZwOpenKey / ZwDeleteValueKey / ZwDeleteKey / ZwDeleteFile / ZwUnloadDriver.
     * Those names in the import table of a driver that belongs to no module are
     * louder than the mapped image itself, and we do not need a trace cleaner. */

    rep_u32(CS2ENV_OFF_REQOP) = 0;
}

/* ============================================================================
 * 瀵煎嚭缁?cs2_env_util.c / cs2_env_clean.c 澶嶇敤鐨勬渶灏忔帴鍙? *
 * 涓轰粈涔堥渶瑕? item_add / find_add / g_report / g_risk 杩欎簺鏄湰鏂囦欢鐨?static,
 * 浣?鐩綍閲屾柊钀界殑 .sys""娉ㄥ唽琛ㄩ敭瀛樺湪鎬?"鍕鹃€夋竻鐞?杩欎簺閫昏緫鏀惧湪鍙﹀涓や釜 .c 閲?
 * 瀹冧滑鏃㈣璇绘姤鍛婅鍥? 涔熻鑳借拷鍔犳姤鍛婇」鍜岀櫥璁板彲娓呯悊鐩爣銆? * ========================================================================== */

void* cs2_env_report_base(void)
{
    return (void*)g_report;
}

void cs2_env_item_add(u32 id, u32 cat, u32 sev, u32 state, u32 cleanable,
                      const char* title, const char* detail)
{
    item_add(id, (u8)cat, (u8)sev, (u8)state, (u8)cleanable, title, detail);
}

void cs2_env_find_add(u32 id, u32 kind, const WCHAR* path, const WCHAR* extra)
{
    find_add(id, kind, path, extra);
}

cs2env_finding_t* cs2_env_finding_list(u32* count)
{
    if (count) { *count = g_findingCount; }
    return g_findings;
}

/* 鍙傝€冧骇鍝佺殑杩涚▼/鏈嶅姟闂ㄦ鍙互鎶婂钩鍙板氨缁害鐩存帴鍒ゆ */
void cs2_env_set_ready(u32 pac, u32 five)
{
    if (pac != CS2ENV_READY_UNKNOWN)  { g_pacReady  = pac; }
    if (five != CS2ENV_READY_UNKNOWN) { g_fiveReady = five; }
}

void cs2_env_flag_reboot(void)
{
    g_flags |= CS2ENV_FLAG_REBOOT;
}

int cs2_env_reg_key_exists(const WCHAR* fullPath)
{
    return reg_key_exists(fullPath);
}

u32 cs2_env_report_risk(void)
{
    return g_risk;
}

