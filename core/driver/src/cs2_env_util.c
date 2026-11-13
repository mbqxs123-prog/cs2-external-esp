/* ============================================================================
 * cs2_env_util.c  --  鐜鑷鐢ㄥ埌鐨勬枃浠剁郴缁?瀵硅薄杈呭姪 + "骞冲彴闂ㄦ"妫€娴? *
 * cs2_env.c 鍙仛"璇绘敞鍐岃〃/璇绘ā鍧楀垪琛?杩欑被鍐呮牳鎬佽瘉鎹敹闆? 浣嗘湁涓夌被鍒ゆ柇蹇呴』
 * 钀藉埌鏂囦欢绯荤粺鍜屽懡鍚嶅璞′笂, 杩欎簺鏀惧湪杩欓噷:
 *   cs2_file_exists()        纾佺洏鍓湰鏄惁杩樺湪 (鎴戞柟 .sys / 鏄犲皠鍣ㄨ惤鍦版枃浠?
 *   cs2_section_openable()   鎴戞柟鍛藉悕 section 鏄惁杩樻椿鐫€
 *   cs2_scan_dir_for_sys()   鐩綍閲屾湁娌℃湁"鏈€杩?N 澶╂柊澧炵殑 .sys"
 *   str_contains()           澶у皬鍐欐棤鍏冲瓙涓插尮閰?(鍐呮牳璋冭瘯閫夐」 / 鏄犲皠鍣ㄥ悕瀛?
 *
 * 鍙﹀ cs2_env_ref_checks() 澶嶅埢鍙傝€冧骇鍝?缃戦〉闆疯揪 01.exe)閭ｅ"骞冲彴闂ㄦ":
 *   瀹冩妸銆愬钩鍙拌繘绋嬫病鍏?/ ACE 娌″嵏杞?/ 鏈夐樆濉為┍鍔ㄥ姞杞界殑绗笁鏂?AC銆戝綋鎴愮‖闂ㄦ,
 *   鐜娓呯悊涓嶈繃灏变笉缁欑偣"楠岃瘉骞跺惎鍔?銆傛垜浠笉鏀圭郴缁熴€佷笉鍗歌浇鍒汉鐨勪笢瑗? 鍙姤鍛?
 *   娓呯悊鍔ㄤ綔鐣欑粰鐢ㄦ埛鍦ㄧ粍浠堕噷鍕鹃€?(鎴戜滑鍙竻"鎴戜滑鑷繁鐨勭棔杩?鍜岀敤鎴锋槑纭嬀閫夌殑椤?銆? * ========================================================================== */
#include <ntifs.h>
#include <ntstrsafe.h>
#include "../inc/cs2core_km.h"
#include "../../../shared/cs2env_abi.h"
#include "cs2_env.h"

/* ------------------------------------------------------------------ */
/* 鑷澹版槑 (ntddk.h 涓嶅惈 ntifs 閲岀殑杩欏嚑涓?                            */
/* ------------------------------------------------------------------ */
NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG Class, PVOID Buffer, ULONG Length, PULONG ReturnLength);
NTSYSAPI NTSTATUS NTAPI ZwQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                             PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                             ULONG Length, FILE_INFORMATION_CLASS FileInformationClass,
                                             BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan);
NTSYSAPI NTSTATUS NTAPI ZwOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                   PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions);
NTSYSAPI NTSTATUS NTAPI ZwQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                               ULONG Length, FILE_INFORMATION_CLASS FileInformationClass);
NTSYSAPI NTSTATUS NTAPI ZwOpenSection(PHANDLE SectionHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);

#define CS2_SYS_PROCESSES 5UL

#define CS2_FILE_BOTH_DIR_INFORMATION_CLASS 3
#define CS2_FILE_BASIC_INFORMATION_CLASS    4

#define CS2_OPEN_FOR_BACKUP_INTENT 0x00004000UL

/* ------------------------------------------------------------------ */
/* 鏈€灏忕粨鏋勯暅鍍?                                                       */
/* ------------------------------------------------------------------ */
typedef struct cs2_file_both_dir_info
{
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    ULONG         EaSize;
    CCHAR         ShortNameLength;
    WCHAR         ShortName[12];
    WCHAR         FileName[1];
} cs2_file_both_dir_info_t;

typedef struct cs2_file_basic_info
{
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG         FileAttributes;
} cs2_file_basic_info_t;

/* 鍙渶瑕佽蛋鍒?ImageName 涓烘 (涓庡師鐢?SYSTEM_PROCESS_INFORMATION 鍓嶇紑涓€鑷? */
typedef struct cs2_proc_prefix
{
    ULONG           NextEntryOffset;
    ULONG           NumberOfThreads;
    LARGE_INTEGER   WorkingSetPrivateSize;
    ULONG           HardFaultCount;
    ULONG           NumberOfThreadsHighWatermark;
    ULONGLONG       CycleTime;
    LARGE_INTEGER   CreateTime;
    LARGE_INTEGER   UserTime;
    LARGE_INTEGER   KernelTime;
    UNICODE_STRING  ImageName;
} cs2_proc_prefix_t;

/* ------------------------------------------------------------------ */
/* 鍩虹瀛楃涓插伐鍏?                                                     */
/* ------------------------------------------------------------------ */

static char cs2_lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') { return (char)(c + 32); }
    return c;
}

/* 澶у皬鍐欐棤鍏崇殑 ASCII 瀛愪覆鍖归厤 (hay/needle 閮芥槸绐勫瓧绗︿覆) */
int str_contains(const char* hay, const char* needle)
{
    ULONG i, j;
    if (!hay || !needle || needle[0] == 0) { return 0; }
    for (i = 0; hay[i] != 0; i++)
    {
        for (j = 0; needle[j] != 0; j++)
        {
            char a = hay[i + j];
            if (a == 0) { break; }
            if (cs2_lower_ascii(a) != cs2_lower_ascii(needle[j])) { break; }
        }
        if (needle[j] == 0) { return 1; }
    }
    return 0;
}

/* 鏀寔 '*' 閫氶厤, 澶у皬鍐欐棤鍏?(妯′豢 cs2_env.c 閲岀殑 name_match_ci, 杩欓噷瑕佸尮閰嶇殑鏄? * 杩涚▼鏂囦欢鍚? 鎵€浠ュ繀椤绘湁閫氶厤) */
static int proc_name_match(const char* name, const char* pat)
{
    ULONG ni = 0, pi = 0, star = 0xFFFFFFFF, mark = 0;
    if (!name || !pat) { return 0; }
    while (name[ni] != 0)
    {
        if (pat[pi] == '*')
        {
            star = pi++;
            mark = ni;
        }
        else if (pat[pi] != 0 && cs2_lower_ascii(pat[pi]) == cs2_lower_ascii(name[ni]))
        {
            pi++; ni++;
        }
        else if (star != 0xFFFFFFFF)
        {
            pi = star + 1;
            ni = ++mark;
        }
        else
        {
            return 0;
        }
    }
    while (pat[pi] == '*') { pi++; }
    return (pat[pi] == 0) ? 1 : 0;
}

static void wide_to_narrow(const WCHAR* w, ULONG wChars, char* out, ULONG outLen)
{
    ULONG i;
    if (!out || outLen == 0) { return; }
    out[0] = 0;
    if (!w) { return; }
    for (i = 0; i + 1 < outLen && i < wChars && w[i] != 0; i++)
    {
        out[i] = (w[i] < 128) ? (char)w[i] : '?';
    }
    out[i] = 0;
}

/* 鍙栬矾寰勭殑鏂囦欢鍚嶉儴鍒?(绐? */
static const char* file_part(const char* path)
{
    const char* last = path;
    ULONG i;
    if (!path) { return ""; }
    for (i = 0; path[i] != 0; i++)
    {
        if (path[i] == '\\' || path[i] == '/') { last = path + i + 1; }
    }
    return last;
}

static int ends_with_ci(const char* s, const char* suffix)
{
    ULONG ls, lf, i;
    if (!s || !suffix) { return 0; }
    ls = (ULONG)strlen(s);
    lf = (ULONG)strlen(suffix);
    if (lf == 0 || ls < lf) { return 0; }
    for (i = 0; i < lf; i++)
    {
        if (cs2_lower_ascii(s[ls - lf + i]) != cs2_lower_ascii(suffix[i])) { return 0; }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 鏂囦欢 / 瀵硅薄                                                         */
/* ------------------------------------------------------------------ */

int cs2_file_exists(const WCHAR* ntPath)
{
    HANDLE           h = NULL;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK  iosb;
    UNICODE_STRING   name;
    NTSTATUS         st;

    if (!ntPath || ntPath[0] == 0) { return 0; }

    RtlInitUnicodeString(&name, ntPath);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenFile(&h,
                    FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    &oa,
                    &iosb,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | CS2_OPEN_FOR_BACKUP_INTENT);
    if (!NT_SUCCESS(st)) { return 0; }

    ZwClose(h);
    return 1;
}

int cs2_section_openable(const WCHAR* name)
{
    HANDLE           h = NULL;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING   un;
    NTSTATUS         st;

    if (!name || name[0] == 0) { return 0; }

    RtlInitUnicodeString(&un, name);
    InitializeObjectAttributes(&oa, &un, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenSection(&h, SECTION_MAP_READ, &oa);
    if (!NT_SUCCESS(st)) { return 0; }

    ZwClose(h);
    return 1;
}

/* 鐩綍閲?鏈€杩?daysBack 澶╂柊澧?鏀瑰姩鐨?.sys"銆傝繑鍥炲懡涓暟閲忋€? * report != 0 鏃跺悓鏃? 鎶婃瘡涓懡涓櫥璁版垚鍙竻鐞嗙殑 finding, 骞跺啓涓€鏉℃姤鍛婇」銆?*/
int cs2_scan_dir_for_sys(const WCHAR* ntDir, u32 daysBack, u32 report)
{
    HANDLE            hDir = NULL;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    UNICODE_STRING    name;
    NTSTATUS          st;
    PVOID             buf = NULL;
    const ULONG       bufLen = 64 * 1024;
    LARGE_INTEGER     now, cutoff;
    ULONG             hits = 0, seen = 0;
    char              firstHit[96];
    char              detail[CS2ENV_DETAIL_LEN];

    if (!ntDir || ntDir[0] == 0) { return 0; }
    firstHit[0] = 0;

    RtlInitUnicodeString(&name, ntDir);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenFile(&hDir,
                    FILE_LIST_DIRECTORY | SYNCHRONIZE,
                    &oa,
                    &iosb,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | CS2_OPEN_FOR_BACKUP_INTENT);
    if (!NT_SUCCESS(st)) { return 0; }

    buf = ExAllocatePoolWithTag(NonPagedPool, bufLen, CS2_POOL_TAG);
    if (!buf) { ZwClose(hDir); return 0; }

    KeQuerySystemTime(&now);
    cutoff.QuadPart = now.QuadPart - ((LONGLONG)daysBack * 24LL * 3600LL * 10000000LL);

    for (;;)
    {
        cs2_file_both_dir_info_t* e;
        ULONG offset = 0;

        st = ZwQueryDirectoryFile(hDir, NULL, NULL, NULL, &iosb, buf, bufLen,
                                  (FILE_INFORMATION_CLASS)CS2_FILE_BOTH_DIR_INFORMATION_CLASS,
                                  FALSE, NULL, (seen == 0) ? TRUE : FALSE);
        if (!NT_SUCCESS(st)) { break; }
        seen = 1;

        for (;;)
        {
            char nm[300];
            char full[2 * CS2ENV_PATH_CHARS];

            e = (cs2_file_both_dir_info_t*)((UCHAR*)buf + offset);
            wide_to_narrow(e->FileName, e->FileNameLength / sizeof(WCHAR), nm, sizeof(nm));

            if (nm[0] != 0 && nm[0] != '.' && ends_with_ci(nm, ".sys"))
            {
                HANDLE            hFile = NULL;
                UNICODE_STRING    fn;
                OBJECT_ATTRIBUTES foa;
                IO_STATUS_BLOCK   fiosb;
                cs2_file_basic_info_t bi;
                WCHAR             wFull[2 * CS2ENV_PATH_CHARS];

                /* 鎷?\??\dir\name */
                {
                    ULONG i = 0;
                    while (ntDir[i] != 0 && i + 1 < (2 * CS2ENV_PATH_CHARS) - 2)
                    {
                        wFull[i] = ntDir[i];
                        i++;
                    }
                    if (i > 0 && wFull[i - 1] != L'\\' && i + 1 < (2 * CS2ENV_PATH_CHARS) - 2)
                    {
                        wFull[i++] = L'\\';
                    }
                    {
                        ULONG j = 0;
                        while (nm[j] != 0 && i + 1 < (2 * CS2ENV_PATH_CHARS) - 1)
                        {
                            wFull[i++] = (WCHAR)nm[j++];
                        }
                    }
                    wFull[i] = 0;
                }

                RtlInitUnicodeString(&fn, wFull);
                InitializeObjectAttributes(&foa, &fn, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
                st = ZwOpenFile(&hFile, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &foa, &fiosb,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | CS2_OPEN_FOR_BACKUP_INTENT);
                if (NT_SUCCESS(st))
                {
                    RtlZeroMemory(&bi, sizeof(bi));
                    if (NT_SUCCESS(ZwQueryInformationFile(hFile, &fiosb, &bi, sizeof(bi),
                                                          (FILE_INFORMATION_CLASS)CS2_FILE_BASIC_INFORMATION_CLASS)))
                    {
                        /* ChangeTime 涔熺湅: 鍙敼鏃堕棿鎴?鍙噸鍛藉悕鐨勮惤鍦颁篃浼氶湶鍑烘潵 */
                        if (bi.LastWriteTime.QuadPart >= cutoff.QuadPart || bi.ChangeTime.QuadPart >= cutoff.QuadPart)
                        {
                            hits++;
                            if (firstHit[0] == 0) { wide_to_narrow(e->FileName, e->FileNameLength / sizeof(WCHAR), firstHit, sizeof(firstHit)); }
                            if (report)
                            {
                                cs2_env_find_add(CS2ENV_ID_NEW_DRIVERS, CS2ENV_KIND_FILE, wFull, L"");
                            }
                        }
                    }
                    ZwClose(hFile);
                }
                (void)full;
            }

            if (e->NextEntryOffset == 0) { break; }
            offset += e->NextEntryOffset;
            if (offset >= bufLen) { break; }
        }
    }

    ExFreePoolWithTag(buf, CS2_POOL_TAG);
    ZwClose(hDir);

    if (report)
    {
        char dirNarrow[128];
        wide_to_narrow(ntDir, 2 * CS2ENV_PATH_CHARS, dirNarrow, sizeof(dirNarrow));

        detail[0] = 0;
        if (hits == 0)
        {
            /* 鐩綍鎵撳紑澶辫触鏃朵笉瑕佽皫鎶?姝ｅ父" */
            if (seen == 0)
            {
                cs2_env_item_add(CS2ENV_ID_NEW_DRIVERS, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_INFO,
                                 CS2ENV_STATE_UNKNOWN, CS2ENV_CLEAN_NO, "近期新增的内核驱动",
                                 "目录打不开, 无法确认");
            }
            return 0;
        }
        RtlStringCchPrintfA(detail, CS2ENV_DETAIL_LEN, "%lu 涓?.sys 鍦ㄦ渶杩?%lu 澶╁唴钀藉湴, 棣栦釜: ",
                            hits, daysBack);
        /* 鎵嬪伐鎷肩獎瀛楃涓? 涓嶄緷璧?ntstrsafe */
        {
            ULONG i = 0, j = 0;
            while (detail[i] != 0 && i + 1 < CS2ENV_DETAIL_LEN) { i++; }
            while (firstHit[j] != 0 && i + 1 < CS2ENV_DETAIL_LEN) { detail[i++] = firstHit[j++]; }
            detail[i] = 0;
        }
        cs2_env_item_add(CS2ENV_ID_NEW_DRIVERS, CS2ENV_CAT_INTEGRITY, CS2ENV_SEV_WARN,
                         CS2ENV_STATE_RISK, CS2ENV_CLEAN_YES, "近期新增的内核驱动", detail);
        (void)dirNarrow;
    }

    return (int)hits;
}

/* ------------------------------------------------------------------ */
/* 鍙傝€冧骇鍝?缃戦〉闆疯揪)閭ｅ骞冲彴闂ㄦ                                       */
/* ------------------------------------------------------------------ */

/* ACE (鑵捐鍙嶄綔寮? 鐨勬墭鐩?瀹堟姢杩涚▼; 鍙傝€冧骇鍝佺殑鐜妫€娴嬮噷涓撻棬鎶?"銆怉CE-Tray.exe銆戞湭鍏抽棴" */
static const char* k_ace_proc[] = { 0 };

/* 骞冲彴杩涚▼ (瀹岀編/5E/faceit/瀹欐柉 绛?銆傚弬鑰冧骇鍝佽姹?蹇呴』鍗歌浇骞冲彴鍐嶅惎鍔?銆?*/
static const char* k_plat_proc[] = { 0 };

/* 浼氶樆濉?椹卞姩鍔犺浇/璁惧绡℃敼淇濇姢"鐨勭涓夋柟 AC 鏈嶅姟 */
static const WCHAR* k_blocker_svc[] = { 0 };

void cs2_env_ref_checks(void)
{
    PVOID  buf = NULL;
    ULONG  len = 0, ret = 0;
    NTSTATUS st;
    ULONG  i, ace = 0, plat = 0, blocker = 0;
    char   aceName[64], platName[64], detail[CS2ENV_DETAIL_LEN];

    aceName[0] = 0;
    platName[0] = 0;

    /* ---- 杩涚▼ ---- */
    st = ZwQuerySystemInformation(CS2_SYS_PROCESSES, NULL, 0, &len);
    if (st == STATUS_INFO_LENGTH_MISMATCH || (len > 0))
    {
        len += 256 * 1024;
        buf = ExAllocatePoolWithTag(NonPagedPool, len, CS2_POOL_TAG);
        if (buf)
        {
            st = ZwQuerySystemInformation(CS2_SYS_PROCESSES, buf, len, &ret);
            if (NT_SUCCESS(st))
            {
                cs2_proc_prefix_t* p = (cs2_proc_prefix_t*)buf;
                for (;;)
                {
                    if (p->ImageName.Buffer && p->ImageName.Length > 0)
                    {
                        char full[300];
                        const char* base;
                        ULONG wChars = p->ImageName.Length / sizeof(WCHAR);

                        wide_to_narrow(p->ImageName.Buffer, wChars, full, sizeof(full));
                        base = file_part(full);

                        for (i = 0; k_ace_proc[i]; i++)
                        {
                            if (proc_name_match(base, k_ace_proc[i]))
                            {
                                ace++;
                                if (ace == 1) { RtlStringCchCopyA(aceName, sizeof(aceName), base); }
                                break;
                            }
                        }
                        for (i = 0; k_plat_proc[i]; i++)
                        {
                            if (proc_name_match(base, k_plat_proc[i]))
                            {
                                plat++;
                                if (plat == 1) { RtlStringCchCopyA(platName, sizeof(platName), base); }
                                break;
                            }
                        }
                    }
                    if (p->NextEntryOffset == 0) { break; }
                    p = (cs2_proc_prefix_t*)((UCHAR*)p + p->NextEntryOffset);
                }
            }
            ExFreePoolWithTag(buf, CS2_POOL_TAG);
        }
    }

    /* ---- 闃诲椹卞姩鍔犺浇鐨勭涓夋柟 AC 鏈嶅姟 ---- */
    for (i = 0; k_blocker_svc[i]; i++)
    {
        WCHAR key[256];
        char  tmp[64];
        ULONG j = 0, k = 0;

        /* \Registry\Machine\SYSTEM\CurrentControlSet\Services\ */
        {
            const WCHAR* pre = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
            while (pre[j] != 0 && j < 200) { key[j] = pre[j]; j++; }
        }
        while (k_blocker_svc[i][k] != 0 && j < 250) { key[j++] = k_blocker_svc[i][k++]; }
        key[j] = 0;

        if (cs2_env_reg_key_exists(key))
        {
            blocker++;
            wide_to_narrow(k_blocker_svc[i], 64, tmp, sizeof(tmp));
            if (blocker == 1) { RtlStringCchCopyA(platName[0] ? platName : aceName, sizeof(aceName), tmp); }
        }
    }

    /* ---- 鎶ュ憡 ---- */
    if (ace > 0)
    {
        RtlStringCchPrintfA(detail, CS2ENV_DETAIL_LEN,
            "发现 %lu 个 ACE 进程 (首个 %s)。参考产品同款门槛: ACE 必须卸载而不是退出; "
            "ACE-Tray.exe 即使退到托盘仍会扫内核; 卸载后重启再启动 Stage-A.", ace, aceName);
        cs2_env_item_add(CS2ENV_ID_ACE_TRAY, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_CRITICAL,
                         CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "ACE 鍙嶄綔寮婃湭鍏抽棴", detail);
    }
    else
    {
        cs2_env_item_add(CS2ENV_ID_ACE_TRAY, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_INFO,
                         CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "ACE 鍙嶄綔寮婃湭鍏抽棴", "鏈彂鐜?ACE 杩涚▼");
    }

    if (plat > 0)
    {
        RtlStringCchPrintfA(detail, CS2ENV_DETAIL_LEN,
            "发现 %lu 个平台进程 (首个 %s)。正确顺序: 关掉平台 → 装/加载 Stage-A → 再开平台; "
            "开着平台加载内核模块会被平台驱动当场看到.", plat, platName);
        cs2_env_item_add(CS2ENV_ID_PLATFORM_PROC, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_CRITICAL,
                         CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "娓告垙骞冲彴姝ｅ湪杩愯", detail);
        cs2_env_set_ready(CS2ENV_READY_BLOCKED, CS2ENV_READY_UNKNOWN);
    }
    else
    {
        cs2_env_item_add(CS2ENV_ID_PLATFORM_PROC, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_INFO,
                         CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "娓告垙骞冲彴姝ｅ湪杩愯", "鏈彂鐜板畬缇?5E/FACEIT/瀹欐柉 杩涚▼");
    }

    if (blocker > 0)
    {
        RtlStringCchPrintfA(detail, CS2ENV_DETAIL_LEN,
            "发现 %lu 个会阻塞驱动加载/设备篡改保护的 AC 服务 (%s ...)。这类服务在跑时, "
            "任何非白名单内核模块都可能被拦或被上报.", blocker, platName);
        cs2_env_item_add(CS2ENV_ID_TAMPER_BLOCKER, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_WARN,
                         CS2ENV_STATE_RISK, CS2ENV_CLEAN_NO, "第三方 AC 会阻塞驱动加载", detail);
    }
    else
    {
        cs2_env_item_add(CS2ENV_ID_TAMPER_BLOCKER, CS2ENV_CAT_ACCONFLICT, CS2ENV_SEV_INFO,
                         CS2ENV_STATE_OK, CS2ENV_CLEAN_NO, "第三方 AC 会阻塞驱动加载", "未发现已知阻塞型 AC 服务");
    }
}

/* 鍙傝€冧骇鍝侀偅鏉?蹇呴』鍏堣繃鐜娓呯悊鍐嶅惎鍔?鐨勯『搴? 浣滀负涓€鏉℃彁绀洪」鍗曠嫭鎶ュ嚭鏉?*/
void cs2_env_ref_workflow_tip(void)
{
    cs2_env_item_add(CS2ENV_ID_PLATFORM_PROC + 100u, CS2ENV_CAT_PLATFORM, CS2ENV_SEV_INFO,
                     CS2ENV_STATE_NA, CS2ENV_CLEAN_NO, "寤鸿鍔犺浇椤哄簭",
                     "鍏冲钩鍙?ACE 鈫?鐜妫€娴嬪叏缁?鈫?鍔犺浇 Stage-A 鈫?鍒犻櫎钀藉湴鏂囦欢 鈫?鍐嶅紑骞冲彴");
}
