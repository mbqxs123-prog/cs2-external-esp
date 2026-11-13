/* ============================================================================
 * kmem.c  --  鏃犲彞鏌勮鍙栫洰鏍囪繘绋嬪唴瀛?+ 妯″潡鏋氫妇
 *
 * 涓夋潯纭€х害鏉?瑙?docs/ANTICHEAT.md 搂3):
 *   1. 涓嶈皟鐢?ZwOpenProcess 鈥斺€?閭ｄ細鍦?cs2.exe 涓婄暀涓?PROCESS_VM_READ 鍙ユ焺,
 *      鍙嶄綔寮婃灇涓剧郴缁熷彞鏌勮〃灏辫兘鐪嬪埌"璋佸湪璇绘垜"銆? *   2. 涓嶈皟鐢?ZwReadProcessMemory / NtReadVirtualMemory 鈥斺€?杩欎袱涓槸 EAC/BE/VAC
 *      鎸傞挬涓?ETW 鐩戞帶鐨勯噸鐐硅矾寰勩€傛垜浠蛋 MmCopyVirtualMemory(鍙渶瑕?PEPROCESS)銆? *   3. 涓嶆墦鏃ュ織銆? * ========================================================================== */
#include "kmem.h"
#include "cs2_ptread.h"   /* attach 后的裸拷贝路径 */
#include "cs2_strs.h"     /* 鐩爣鍚?妯″潡鍚嶇殑瀵嗘枃琛?(binary 閲屾棤鏄庢枃) */

/* --- 鑷繁澹版槑, 鍏嶅緱涓轰簡涓や笁涓嚱鏁板幓渚濊禆 ntifs/ntstrsafe 鐨勫叏濂?--- */
typedef struct _CS2_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWCH   Buffer;
} CS2_UNICODE_STRING;

typedef struct _CS2_LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY     InLoadOrderLinks;
    LIST_ENTRY     InMemoryOrderLinks;
    LIST_ENTRY     InInitializationOrderLinks;
    PVOID          DllBase;
    PVOID          EntryPoint;
    ULONG          SizeOfImage;
    CS2_UNICODE_STRING FullDllName;
    CS2_UNICODE_STRING BaseDllName;
    ULONG          Flags;
    USHORT         LoadCount;
    USHORT         TlsIndex;
} CS2_LDR_DATA_TABLE_ENTRY;

typedef struct _CS2_PEB_LDR {
    ULONG  Length;
    BOOLEAN Initialized;
    PVOID  SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
} CS2_PEB_LDR;

NTKERNELAPI PPEB PsGetProcessPeb(PEPROCESS Process);
NTKERNELAPI NTSTATUS ZwQuerySystemInformation(ULONG SystemInformationClass,
                                              PVOID SystemInformation,
                                              ULONG SystemInformationLength,
                                              PULONG ReturnLength);
NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess,
                                         PVOID SourceAddress,
                                         PEPROCESS TargetProcess,
                                         PVOID TargetAddress,
                                         SIZE_T BufferSize,
                                         KPROCESSOR_MODE PreviousMode,
                                         PSIZE_T ReturnSize);

#define SystemProcessInformation_ 5

/* --- ASCII 灏忓啓鍖栨瘮杈?(鍐呮牳閲屼笉瑕佺敤 _stricmp) ----------------------------- */
static char cs2_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int cs2_streq_ci(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (cs2_lower(*a) != cs2_lower(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* ==========================================================================
 * 鐩爣杩涚▼瀹氫綅
 *
 * 鐢?ZwQuerySystemInformation(SystemProcessInformation) 鏋氫妇:
 *   - 涓嶄骇鐢熶换浣曞彞鏌?
 *   - 姣旀墜宸ヨ蛋 PsActiveProcessHead + EPROCESS->ActiveProcessLinks 鏇存姉鐗堟湰鍙樺寲
 *     (閭ｄ釜瀛楁鍋忕Щ姣忎釜 Windows 绱Н鏇存柊閮藉彲鑳藉姩)銆? * 鎷垮埌 pid 鍚?PsLookupProcessByProcessId 寰楀埌 EPROCESS(甯﹀紩鐢ㄨ鏁?銆? * ======================================================================== */

typedef struct _CS2_SYSTEM_PROCESS_INFORMATION {
    ULONG          NextEntryOffset;
    ULONG          NumberOfThreads;
    LARGE_INTEGER  WorkingSetPrivateSize;
    ULONG          HardFaultCount;
    ULONG          NumberOfThreadsHighWatermark;
    ULONGLONG      CycleTime;
    LARGE_INTEGER  CreateTime;
    LARGE_INTEGER  UserTime;
    LARGE_INTEGER  KernelTime;
    CS2_UNICODE_STRING ImageName;
    KPRIORITY      BasePriority;
    HANDLE         UniqueProcessId;
    PVOID          Reserved3;
    ULONG          HandleCount;
    ULONG          SessionId;
    PVOID          Reserved4;
    SIZE_T         PeakVirtualSize;
    SIZE_T         VirtualSize;
} CS2_SYSTEM_PROCESS_INFORMATION;

static ULONG cs2_find_pid_by_name(const WCHAR* exeName) {
    NTSTATUS st;
    ULONG    need = 0;
    PVOID    buf = NULL;
    ULONG    size = 0;
    ULONG    pid = 0;
    int      attempt;

    /* 枚举进程：缓冲区大小必须"够了才算数"。
     * 旧实现只要 ZwQuerySystemInformation 返回成功就按 NextEntryOffset 一路走，
     * 一旦缓冲区被截断（进程数变多 / 系统在高负载）就会走出缓冲区，
     * 把垃圾当成 NextEntryOffset 继续跳 —— 直接 0x50 PAGE_FAULT_IN_NONPAGED_AREA。
     * 现在：长度不足就重试（3 次、每次翻倍），走链时每一步都做边界检查。 */
    for (attempt = 0; attempt < 3 && pid == 0; attempt++) {
        ULONG want;
        need = 0;
        (void)ZwQuerySystemInformation(SystemProcessInformation_, NULL, 0, &need);
        want = (need ? need : 0x20000) + 0x10000 + (ULONG)attempt * 0x20000;

        buf = cs2_alloc_np(want);
        if (!buf) return 0;

        size = 0;
        st = ZwQuerySystemInformation(SystemProcessInformation_, buf, want, &size);
        if (st == STATUS_INFO_LENGTH_MISMATCH || (NT_SUCCESS(st) && size == 0)) {
            cs2_free_wipe(buf, want);      /* holds every process name, incl. ours */
            buf = NULL;
            continue;                      /* 换更大的缓冲区再来一次 */
        }
        if (!NT_SUCCESS(st)) {
            cs2_free_wipe(buf, want);
            buf = NULL;
            continue;
        }

        {
            ULONG off = 0;
            ULONG limit = (size && size <= want) ? size : want;

            for (;;) {
                CS2_SYSTEM_PROCESS_INFORMATION* cur;
                ULONG next;

                if (off + sizeof(CS2_SYSTEM_PROCESS_INFORMATION) > limit) break;   /* 边界 */
                cur = (CS2_SYSTEM_PROCESS_INFORMATION*)((PUCHAR)buf + off);

                if (cur->ImageName.Length > 0 &&
                    cur->ImageName.Buffer != NULL &&
                    (ULONG_PTR)cur->ImageName.Buffer >= (ULONG_PTR)buf &&
                    (ULONG_PTR)cur->ImageName.Buffer + cur->ImageName.Length <= (ULONG_PTR)buf + limit)
                {
                    USHORT chars = cur->ImageName.Length / sizeof(WCHAR);
                    if (chars == 7) {
                        const WCHAR* wantName = exeName;
                        int i, match = 1;
                        for (i = 0; i < 7; i++) {
                            WCHAR a = cur->ImageName.Buffer[i];
                            WCHAR b = wantName[i];
                            if (a >= L'A' && a <= L'Z') a = (WCHAR)(a - L'A' + L'a');
                            if (b >= L'A' && b <= L'Z') b = (WCHAR)(b - L'A' + L'a');
                            if (a != b) { match = 0; break; }
                        }
                        if (match) { pid = (ULONG)(ULONG_PTR)cur->UniqueProcessId; break; }
                    }
                }

                next = cur->NextEntryOffset;
                if (next == 0) break;
                if ((next & 3u) != 0u) break;                    /* 不对齐 = 结构已坏 */
                if (next > limit || off > limit - next) break;    /* 越界 */
                off += next;
            }
        }

        cs2_free_wipe(buf, want);
        buf = NULL;
    }

    return pid;
}

NTSTATUS cs2_target_open(cs2_target* t) {
    ULONG pid;
    NTSTATUS st;

    RtlZeroMemory(t, sizeof(*t));
    pid = cs2_find_pid_by_name(cs2_s_exe_w());
    if (!pid) return STATUS_NOT_FOUND;

    st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &t->process);
    if (!NT_SUCCESS(st)) return st;

    t->pid = pid;

    /* 涓绘ā鍧楀熀鍧€: PEB 閲岀涓€涓姞杞介」灏辨槸 exe 鑷繁, 鐩存帴鍙?*/
    if (!cs2_module_find(t, cs2_s_exe_a(), NULL)) {
        cs2_target_close(t);
        return STATUS_NOT_FOUND;
    }

    /* 涓绘ā鍧?size 浠?PE 澶磋 */
    {
        cs2_module self;
        if (cs2_module_find(t, cs2_s_exe_a(), &self)) {
            t->base = self.base;
            t->size = self.size;
        }
    }
    return STATUS_SUCCESS;
}

/* 目标进程还活着吗？游戏更新会杀掉旧进程、起新的；此时缓存的 PEPROCESS 虽然
 * 还持有引用，但 PEB / 映像都已释放，继续读只会拿到垃圾。每 tick 检查一次即可。 */
int cs2_target_alive(cs2_target* t) {
    if (!t || !t->process) return 0;
    if ((ULONG)(ULONG_PTR)PsGetProcessId(t->process) != t->pid) return 0;
    if (!NT_SUCCESS(PsGetProcessExitStatus(t->process))) return 0;
    return 1;
}

void cs2_target_close(cs2_target* t) {
    if (t->process) {
        ObDereferenceObject(t->process);
        t->process = NULL;
    }
    t->pid = 0; t->base = 0; t->size = 0;
}

/* ==========================================================================
 * 鍐呭瓨璇诲彇
 * ======================================================================== */
int cs2_read(cs2_target* t, u64 va, void* dst, u32 len) {
    SIZE_T got = 0;
    NTSTATUS st;

    if (!t || !t->process || !va || !len) return CS2_FAIL;
    if (va < 0x10000) return CS2_FAIL;     /* 鎺掗櫎绌烘寚閽堥檮杩? 灏戜竴娆℃棤璋撹皟鐢?*/

    /* attach 态: 走裸拷贝, 不经过 MmCopyVirtualMemory (见 cs2_ptread.c 顶部) */
    if (cs2_pt_attached()) {
        return cs2_pt_read(va, dst, len);
    }

    st = MmCopyVirtualMemory(t->process, (PVOID)va,
                            PsGetCurrentProcess(), dst,
                            (SIZE_T)len, KernelMode, &got);
    if (!NT_SUCCESS(st) || got != (SIZE_T)len) return CS2_FAIL;
    return CS2_OK;
}

/* --------------------------------------------------------------------------
 * 蹇€熻矾寰勩€? *
 * 璇存槑(鍒嚜娆烘浜?: 杩欎釜鍑芥暟鐩墠灏辨槸 cs2_read 鐨勫埆鍚嶃€傜湡姝ｇ殑鍔犻€熷仛娉曟槸
 * 鎵嬪姩璧扮洰鏍囪繘绋嬬殑鍥涚骇椤佃〃, 鎶婃湁鏁堥〉鐩存帴 memcpy 鍒版湰鍦?鈥斺€?鑳界渷鎺? * MmCopyVirtualMemory 姣忔鐨勬崟鑾?鏍￠獙寮€閿€銆備袱鏉″彲琛岃矾绾? 鍦ㄧ紪璇戞満涓婁簩閫変竴:
 *
 *   A) 瑙ｆ瀽 MiGetPteAddress: 瀹冩槸 ntoskrnl 閲岀殑鍐呴儴鍑芥暟, 娌℃湁瀵煎嚭銆? *      鍋氭硶鏄壒寰佺爜鎵弿 ntoskrnl 鎵惧埌瀹? 鎴栧湪宸茬煡鍐呮牳 VA(姣斿 MiGetPteAddress
 *      鑷韩鐨?PTE 浣嶇疆)涓婂仛鑷唇瑙ｆ柟绋嬫眰鍑?PTE self-ref base銆? *   B) 鐩存帴鐢?__readcr3() 鎷跨洰鏍?CR3(DirectoryTableBase), 鍐嶇敤
 *      MmGetVirtualForPhysical 閫愮骇缈昏瘧 鈥斺€?浣嗗繀椤讳繚璇佽繘绋嬪湪璺戙€侀〉鐩綍涓嶈鎹㈠嚭,
 *      宸ョ▼涓婅鍔?KeAreApcsDisabled / 寮曠敤璁℃暟淇濇姢, 鍚﹀垯 BSOD銆? *
 * 鍦ㄨ兘缂栬瘧涔嬪墠涓嶅鏈粡娴嬭瘯鐨勯〉琛ㄤ唬鐮佽繘鏉? 鍐呮牳閲屽啓閿欎竴涓綅灏辨槸钃濆睆,
 * 鑰岄噰闆嗛鐜?姣?tick 涓€娆? 鍑犲崄娆″皬璇?鐢?MmCopyVirtualMemory 瀹屽叏澶熺敤銆? * ------------------------------------------------------------------------ */
int cs2_read_fast(cs2_target* t, u64 va, void* dst, u32 len) {
    return cs2_read(t, va, dst, len);
}

int cs2_read_ptr(cs2_target* t, u64 va, u64* out) {
    u64 v = 0;
    if (!cs2_read(t, va, &v, sizeof(v))) return CS2_FAIL;
    *out = v;
    return v ? CS2_OK : CS2_FAIL;
}

int cs2_read_cstr(cs2_target* t, u64 va, char* dst, u32 dstLen) {
    u32 i;
    if (!dst || dstLen < 2) return CS2_FAIL;
    dst[0] = 0;
    for (i = 0; i + 1 < dstLen; i++) {
        char c = 0;
        if (!cs2_read(t, va + i, &c, 1)) break;
        if (c == 0) break;
        dst[i] = c;
    }
    dst[(i < dstLen) ? i : dstLen - 1] = 0;
    return dst[0] ? CS2_OK : CS2_FAIL;
}

/* ==========================================================================
 * 妯″潡鏋氫妇: 璧?PEB->Ldr->InLoadOrderModuleList
 *
 * 娉ㄦ剰椤哄簭: 鎴戜滑瑕佽鐨勬槸"鐩爣杩涚▼鍦板潃绌洪棿閲岀殑閾捐〃", 鎵€浠ユ瘡娆¤В寮曠敤閮借
 * 閫氳繃 cs2_read 璧扮洰鏍囪繘绋? 鑰屼笉鏄洿鎺?*ptr 鈥斺€?鐩存帴瑙ｅ紩鐢ㄤ細璇绘垜浠嚜宸辩殑
 * 鍦板潃绌洪棿(钃濆睆鎴栬鍒板瀮鍦?銆? * ======================================================================== */
int cs2_module_find(cs2_target* t, const char* name, cs2_module* out) {
    PPEB peb = NULL;
    CS2_PEB_LDR ldr;
    LIST_ENTRY cur;
    u64 base = 0, pebAddr = 0;
    int guard = 0;

    if (!t || !t->process) return CS2_FAIL;

    peb = PsGetProcessPeb(t->process);
    if (!peb) return CS2_FAIL;
    pebAddr = (u64)peb;

    /* 璇?PEB 閲岀殑 Ldr 鎸囬拡 (鍋忕Щ 0x18 on x64) */
    if (!cs2_read(t, pebAddr + 0x18, &base, sizeof(base)) || !base) return CS2_FAIL;
    if (!cs2_read(t, base, &ldr, sizeof(ldr))) return CS2_FAIL;

    if (!cs2_read(t, base + FIELD_OFFSET(CS2_PEB_LDR, InLoadOrderModuleList), &cur, sizeof(cur)))
        return CS2_FAIL;

    {
        u64 list_head = base + FIELD_OFFSET(CS2_PEB_LDR, InLoadOrderModuleList);
        while (cur.Flink && (u64)(ULONG_PTR)cur.Flink != list_head) {
        CS2_LDR_DATA_TABLE_ENTRY e;
        char ansi[64];
        u64 entryAddr = (u64)cur.Flink - FIELD_OFFSET(CS2_LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if (!cs2_read(t, entryAddr, &e, sizeof(e))) break;

        if (e.BaseDllName.Buffer && e.BaseDllName.Length > 0 && e.BaseDllName.Length < 128) {
            /* UNICODE_STRING -> ASCII (妯″潡鍚嶉兘鏄?ASCII, 鎵嬪伐鎴柇鍗冲彲, 閬垮厤 RtlUnicodeToAscii) */
            USHORT i, n = e.BaseDllName.Length / sizeof(WCHAR);
            if (n > 62) n = 62;
            for (i = 0; i < n; i++) {
                WCHAR wc = 0;
                if (!cs2_read(t, (u64)e.BaseDllName.Buffer + (u64)i * sizeof(WCHAR), &wc, sizeof(wc)))
                    break;
                ansi[i] = (wc < 128) ? (char)wc : '?';
            }
            ansi[n] = 0;

            if (cs2_streq_ci(ansi, name)) {
                if (out) {
                    RtlZeroMemory(out, sizeof(*out));
                    out->base = (u64)e.DllBase;
                    out->size = e.SizeOfImage;
                    out->entry = (u64)e.EntryPoint;
                    out->sections = 0;
                    out->timestamp = 0;
                    /* PE 鏃堕棿鎴? 鐢ㄦ潵缁欑壒寰佺爜搴撳仛"杩欑増 client.dll 璁ゅ緱鍚?鐨勫垽鏂?*/
                    {
                        IMAGE_DOS_HEADER dos;
                        IMAGE_NT_HEADERS64 nt;
                        if (cs2_read(t, out->base, &dos, sizeof(dos)) &&
                            dos.e_magic == IMAGE_DOS_SIGNATURE &&
                            cs2_read(t, out->base + (u64)dos.e_lfanew, &nt, sizeof(nt)) &&
                            nt.Signature == IMAGE_NT_SIGNATURE) {
                            out->timestamp = nt.FileHeader.TimeDateStamp;
                            out->sections = nt.FileHeader.NumberOfSections;
                        }
                    }
                }
                return CS2_OK;
            }
        }

        cur = e.InLoadOrderLinks;
        if (++guard > 512) break;      /* 閾捐〃鎹熷潖淇濇姢 */
    }
        }
    return CS2_FAIL;
}

/* ==========================================================================
 * 闄勫姞/鑴辩鐩爣鍦板潃绌洪棿 (鎵弿鍣?PE 澶磋В鏋愭椂闇€瑕?
 * ======================================================================== */
static KAPC_STATE g_Apc;
static int        g_Attached = 0;

/* v5.2: attach/detach/probe_range_attached 死代码已删除（全项目无调用点，
 * 少一个可被 hook 的入口，也少一份 KeStackAttachProcess 的镜像引用）。 */
