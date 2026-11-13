/* ============================================================================
 * cs2_env_clean.c  --  鐜鑷鐨?鍕鹃€夊悗瀹氱偣娓呯悊"
 *
 * 鍘熷垯(鍜?cs2_env.c 涓€鑷?:
 *   1. 鍙姩 cs2_env.c 鎵弿鏃?*宸茬粡鐧昏鎴?finding 鐨勭洰鏍?*銆傛病鏈?finding 鐨勯」
 *      鍗充娇缁勪欢鍕句簡涔熶笉鍔?鈥斺€?鍐呮牳涓嶄細鍘诲垹涓€涓畠娌￠獙璇佽繃鐨勮矾寰勩€? *   2. 鍙竻"鎴戜滑鑷繁鐨勭棔杩?鍜?鐢ㄦ埛鏄庣‘鍕鹃€夌殑娈嬬暀", 涓嶇绯荤粺绛栫暐銆佷笉鍗歌浇鍒汉鐨? *      骞冲彴(鍙傝€冧骇鍝?01.exe 浼氶『鎵嬫妸 5E/瀹岀編 鍗告帀, 鎴戜滑涓嶈繖涔堝共: 閭ｆ槸鐢ㄦ埛鐨勮蒋浠?
 *      鑰屼笖鍗歌浇鍔ㄤ綔鏈韩浼氱暀涓嬬棔杩?銆? *   3. 鍒犱笉鎺夊氨濡傚疄鎶ュけ璐?CS2ENV_RES_FAIL), 骞惰缁勪欢鎻愮ず鐢ㄦ埛鍘昏窇
 *      tools\clean-env.ps1 鈥斺€?鏈夊瓙閿殑鏈嶅姟閿湪瀹夊叏妯″紡涓嬫墠鍒犲緱骞插噣銆? *   4. 鍔ㄤ簡"瑕侀噸鍚墠鐢熸晥"鐨勪笢瑗?渚嬪宸插姞杞界殑椹卞姩瀵硅薄)灏辩疆 CS2ENV_FLAG_REBOOT銆? *
 * 鎶ュ憡绾﹀畾(瑙?shared/cs2env_abi.h):
 *   缁勪欢鍐?item.SELECT = 1  鈫?鏈枃浠舵墽琛?鈫?鍥炲～ item.RESULT / item.STATE
 * ========================================================================== */
#include <ntifs.h>
#include "../inc/cs2core_km.h"
#include "../../../shared/cs2env_abi.h"
#include "cs2_env.h"

NTSYSAPI NTSTATUS NTAPI ZwDeleteFile(POBJECT_ATTRIBUTES ObjectAttributes);
NTSYSAPI NTSTATUS NTAPI ZwOpenKey(PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
NTSYSAPI NTSTATUS NTAPI ZwDeleteKey(HANDLE KeyHandle);
NTSYSAPI NTSTATUS NTAPI ZwDeleteValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName);
NTSYSAPI NTSTATUS NTAPI ZwUnloadDriver(PUNICODE_STRING DriverServiceName);

#define CS2_DELETE 0x00010000UL

/* ------------------------------------------------------------------ */

static UCHAR* item_at(UCHAR* rep, u32 idx)
{
    return rep + CS2ENV_HEADER_SIZE + (idx * CS2ENV_ITEM_SIZE);
}

static int del_file(const WCHAR* ntPath)
{
    UNICODE_STRING   name;
    OBJECT_ATTRIBUTES oa;
    NTSTATUS         st;

    if (!ntPath || ntPath[0] == 0) { return 0; }

    RtlInitUnicodeString(&name, ntPath);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwDeleteFile(&oa);
    if (st == STATUS_OBJECT_NAME_NOT_FOUND) { return 1; }   /* 宸茬粡涓嶅湪浜嗕篃绠楁垚鍔?*/
    return NT_SUCCESS(st) ? 1 : 0;
}

static int del_key(const WCHAR* ntPath)
{
    UNICODE_STRING   name;
    OBJECT_ATTRIBUTES oa;
    HANDLE           h = NULL;
    NTSTATUS         st;

    if (!ntPath || ntPath[0] == 0) { return 0; }

    RtlInitUnicodeString(&name, ntPath);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenKey(&h, CS2_DELETE, &oa);
    if (st == STATUS_OBJECT_NAME_NOT_FOUND) { return 1; }
    if (!NT_SUCCESS(st)) { return 0; }

    st = ZwDeleteKey(h);
    ZwClose(h);
    if (st == STATUS_OBJECT_NAME_NOT_FOUND) { return 1; }
    return NT_SUCCESS(st) ? 1 : 0;   /* STATUS_CANNOT_DELETE = 鏈夊瓙閿? 浜ょ粰 clean-env.ps1 */
}

static int del_value(const WCHAR* ntKeyPath, const WCHAR* valueName)
{
    UNICODE_STRING   name, val;
    OBJECT_ATTRIBUTES oa;
    HANDLE           h = NULL;
    NTSTATUS         st;

    if (!ntKeyPath || ntKeyPath[0] == 0 || !valueName || valueName[0] == 0) { return 0; }

    RtlInitUnicodeString(&name, ntKeyPath);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    st = ZwOpenKey(&h, CS2_DELETE, &oa);
    if (!NT_SUCCESS(st)) { return 0; }

    RtlInitUnicodeString(&val, valueName);
    st = ZwDeleteValueKey(h, &val);
    ZwClose(h);
    if (st == STATUS_OBJECT_NAME_NOT_FOUND) { return 1; }
    return NT_SUCCESS(st) ? 1 : 0;
}

/* 鏈嶅姟閿矾寰勬嫾瑁? \Registry\Machine\SYSTEM\CurrentControlSet\Services\<name> */
static void service_key_path(const WCHAR* svcName, WCHAR* out, ULONG outChars)
{
    const WCHAR* pre = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
    ULONG i = 0, j = 0;
    if (!out || outChars == 0) { return; }
    out[0] = 0;
    if (!pre || !svcName) { return; }
    while (pre[i] != 0 && i + 1 < outChars) { out[i] = pre[i]; i++; }
    while (svcName[j] != 0 && i + 1 < outChars) { out[i++] = svcName[j++]; }
    out[i] = 0;
}

static int del_service(const WCHAR* svcName)
{
    WCHAR key[320];
    service_key_path(svcName, key, 320);
    return del_key(key);
}

/* 宸插姞杞界殑鍐呮牳椹卞姩: 璇曠潃璁╄嚜宸卞嵏杞?鍙湁闈炲叧閿┍鍔ㄤ細鎴愬姛, 涓斿鏈┍鍔ㄦ棤鏁? */
static int try_unload_driver(const WCHAR* svcName)
{
    UNICODE_STRING name;
    NTSTATUS       st;

    if (!svcName || svcName[0] == 0) { return 0; }
    RtlInitUnicodeString(&name, svcName);
    st = ZwUnloadDriver(&name);
    return NT_SUCCESS(st) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* 涓诲叆鍙? 缁勪欢鍐?select 涔嬪悗璋?                                    */
/* ------------------------------------------------------------------ */

int cs2_env_clean_selected(void)
{
    UCHAR* rep = (UCHAR*)cs2_env_report_base();
    cs2env_finding_t* fs;
    u32  fcount = 0, i, k;
    u32  acted = 0, failed = 0;
    int  needReboot = 0;

    if (!rep) { return 0; }

    fs = cs2_env_finding_list(&fcount);

    for (i = 0; i < CS2ENV_MAX_ITEMS; i++)
    {
        UCHAR* it = item_at(rep, i);
        u32    id  = *(u32*)(it + CS2ENV_OFF_I_ID);
        u8     sel = it[CS2ENV_OFF_I_SELECT];
        u8     cleanable = it[CS2ENV_OFF_I_CLEANABLE];
        int    any = 0, ok = 1;

        if (id == 0 || sel == 0) { continue; }
        if (cleanable == CS2ENV_CLEAN_NO) { continue; }   /* 涓嶅彲娓呯殑椤硅繛璇曢兘涓嶈瘯 */

        for (k = 0; k < fcount; k++)
        {
            int step = 1;

            if (fs[k].used == 0 || fs[k].id != id) { continue; }
            any = 1;

            switch (fs[k].kind)
            {
            case CS2ENV_KIND_FILE:
                step = del_file(fs[k].path);
                break;

            case CS2ENV_KIND_DIR:
                step = del_file(fs[k].path);           /* 绌虹洰褰曟墠鑳藉垹鎺?*/
                break;

            case CS2ENV_KIND_SERVICE:
                step = del_service(fs[k].path);
                break;

            case CS2ENV_KIND_REGKEY:
                step = del_key(fs[k].path);
                break;

            case CS2ENV_KIND_REGSUB:
                step = del_value(fs[k].path, fs[k].extra);
                break;

            case CS2ENV_KIND_DRIVER:
                /* 椹卞姩瀵硅薄鏈韩: 璇曠潃鍗? 鍗镐笉鎺夊氨浜ょ粰閲嶅惎(閲嶅惎鍚庝笉鍔犺浇=骞插噣) */
                if (try_unload_driver(fs[k].path)) { step = 1; }
                else { step = 0; needReboot = 1; }
                break;

            default:
                step = 0;
                break;
            }

            if (!step) { ok = 0; }

            /* 鏃犺鎴愯触, 鐩爣鍙敤涓€娆? 闃叉閲嶅鍔ㄤ綔 */
            fs[k].used = 0;
        }

        if (!any)
        {
            it[CS2ENV_OFF_I_RESULT] = (u8)CS2ENV_RES_SKIP;
            continue;
        }

        if (ok)
        {
            acted++;
            it[CS2ENV_OFF_I_RESULT] = (u8)CS2ENV_RES_DONE;
            it[CS2ENV_OFF_I_STATE]  = (u8)CS2ENV_STATE_FIXED;
        }
        else
        {
            failed++;
            it[CS2ENV_OFF_I_RESULT] = (u8)CS2ENV_RES_FAIL;
            /* 澶辫触鐨勫師鍥犱節鎴愭槸"鏈嶅姟閿湁瀛愰敭"鎴?鏂囦欢琚崰鐢?, 浜ょ粰鐢ㄦ埛鎬佽剼鏈?*/
        }
    }

    if (needReboot) { cs2_env_flag_reboot(); }

    /* 鍐欑粨鏋滄湡闂翠笉瑕佸姩 SELECT, 缁勪欢璇诲畬鍚庤嚜宸辨竻 */
    return (int)(acted ? acted : failed ? failed : 0);
}
