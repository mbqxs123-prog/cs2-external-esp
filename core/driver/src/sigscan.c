/* ============================================================================
 * sigscan.c  --  鐗瑰緛鐮佹壂鎻忓疄鐜? *
 * 鍏抽敭鐐?
 *   1. 鎵弿瑕佽鐨勬槸銆愮洰鏍囪繘绋嬨€戠殑鍦板潃绌洪棿, 鑰屾垜浠繍琛屽湪鍐呮牳涓婁笅鏂囬噷銆? *      鎵€浠ユ壂鎻忔湡闂?KeStackAttachProcess 鍒扮洰鏍囪繘绋? 鐒跺悗鐢ㄦ櫘閫氱殑
 *      <<鍐呭瓨瑙ｅ紩鐢?> 鏄€愬嵄闄┿€戠殑(鐩爣鍙兘鍦ㄨ娓呯┖/鎹㈤〉)銆? *      绋冲Ε鍋氭硶瑙?cs2_scan_read(): 鍦?attach 鐘舵€佷笅鎵嬪姩鏍￠獙椤垫湁鏁堟€?
 *      浣嗕负浜嗙畝鍗曚笌瀹夊叏, 鏈疄鐜颁粛鐒惰蛋 cs2_read(=MmCopyVirtualMemory),
 *      attach 鍙敤鏉ヤ繚璇佹垜浠兘涓庣洰鏍囪繘绋嬬殑鍦板潃绌洪棿姣旇緝銆? *      瀹為檯娴嬩笅鏉?MmCopyVirtualMemory 鍦ㄦ壒閲忔壂鎻忔椂鏄富瑕佸紑閿€,
 *      鎵€浠ユ壂鎻忕矑搴︾敤 chunk (榛樿 1MB) 涓€娆℃嫹杩囨潵鍦ㄦ湰鍦版瘮瀵广€? *   2. chunk 杈圭晫澶勭殑鍛戒腑瑕佺壒娈婂鐞? 鎴戜滑鎶婁笂涓€娆?chunk 灏惧反
 *      (patLen-1 瀛楄妭) 鎺ュ埌鏈寮€澶撮噸鎵? 閬垮厤婕忔帀璺ㄧ晫鍛戒腑銆? * ========================================================================== */
#include "../inc/cs2core_km.h"
#include "sigscan.h"

#define CS2_SCAN_CHUNK  0x100000   /* 1 MiB */

/* --- 鐗瑰緛鐮佹枃鏈?-> 瀛楄妭+鎺╃爜 -------------------------------------------- */
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int cs2_pattern_compile(const char* text, cs2_pattern* out) {
    u32 n = 0;
    const char* p = text;
    if (!text || !out) return CS2_FAIL;
    out->valid = 0; out->len = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (n >= CS2_SIG_MAX_BYTES) return CS2_FAIL;

        if (*p == '?' || *p == '*') {          /* 閫氶厤: ?? 鎴?? 鎴?* */
            out->bytes[n] = 0x00; out->mask[n] = 0x00; n++;
            p++;
            if (*p == '?') p++;                /* 鍚冩帀绗簩涓?? */
            continue;
        }
        {
            int hi = hexval(p[0]);
            int lo = (p[1] ? hexval(p[1]) : -1);
            if (hi < 0 || lo < 0) return CS2_FAIL;
            out->bytes[n] = (u8)((hi << 4) | lo);
            out->mask[n] = 0xFF;
            n++;
            p += 2;
        }
    }
    if (n < 4) return CS2_FAIL;               /* 澶煭鐨勭壒寰佺爜瀹佸彲涓嶇敤, 浼氳鍛戒腑 */
    out->len = n;
    out->valid = 1;
    return CS2_OK;
}

/* --- 鍗曚釜缂撳啿鍐呯殑鍖归厤 --------------------------------------------------- */
static u32 scan_buffer(const u8* buf, u32 len, const cs2_pattern* pat,
                       u64 bufVa, int maxHits, cs2_sig_result* out) {
    u32 i;
    u32 found = 0;
    if (len < pat->len) return 0;

    for (i = 0; i + pat->len <= len; i++) {
        u32 j;
        for (j = 0; j < pat->len; j++) {
            if (pat->mask[j] == 0x00) continue;
            if (buf[i + j] != pat->bytes[j]) break;
        }
        if (j == pat->len) {
            if (out->count < maxHits && out->count < CS2_SIG_MAX_HITS) {
                out->hits[out->count].address = bufVa + i;
                out->hits[out->count].rva     = (u32)(bufVa + i);
                out->count++;
            }
            found++;
            if (out->count >= maxHits) return found;
            i += pat->len - 1;               /* 閲嶅彔鍖归厤鏃犳剰涔? 璺宠繃 */
        }
    }
    return found;
}

/* --- 鑺傝〃閬嶅巻 ----------------------------------------------------------- */
/* 杩斿洖 0 琛ㄧず澶辫触; 鎴愬姛鏃跺姣忎釜鑺傝皟鐢?cb(ctx, va, size, characteristics) */
typedef int (*cs2_section_cb)(void* ctx, u64 va, u32 size, u32 characteristics);

static int walk_sections(cs2_target* t, const cs2_module* mod, cs2_section_cb cb, void* ctx) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    u32 i;
    int ok = 0;

    if (!cs2_read(t, mod->base, &dos, sizeof(dos))) return 0;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (!cs2_read(t, mod->base + (u64)dos.e_lfanew, &nt, sizeof(nt))) return 0;
    if (nt.Signature != IMAGE_NT_SIGNATURE) return 0;
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return 0;

    for (i = 0; i < nt.FileHeader.NumberOfSections && i < 96; i++) {
        IMAGE_SECTION_HEADER sh;
        u64 va = mod->base + (u64)nt.OptionalHeader.BaseOfCode; /* 鍗犱綅, 涓嬮潰瑕嗙洊 */
        u64 off = (u64)dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64) +
                  (u64)i * sizeof(IMAGE_SECTION_HEADER);
        u32 sz;
        if (!cs2_read(t, mod->base + off, &sh, sizeof(sh))) break;
        va = mod->base + sh.VirtualAddress;
        sz = sh.Misc.VirtualSize ? sh.Misc.VirtualSize : sh.SizeOfRawData;
        if (!sz) continue;
        if (cb(ctx, va, sz, sh.Characteristics)) ok = 1;
    }
    return ok;
}

typedef struct scan_ctx {
    cs2_target*        t;
    const cs2_pattern* pat;
    int                maxHits;
    cs2_sig_result*    out;
    int                textOnly;
    u8                 tail[CS2_SIG_MAX_BYTES];
    u32                tailLen;
    u64                tailVa;
} scan_ctx;

static int cb_scan(void* vctx, u64 va, u32 size, u32 characteristics) {
    scan_ctx* c = (scan_ctx*)vctx;
    u8* chunk;
    u32 off;

    if (c->textOnly && !(characteristics & IMAGE_SCN_MEM_EXECUTE)) return 0;
    if (c->out->count >= c->maxHits) return 1;

    chunk = (u8*)cs2_alloc_np(CS2_SCAN_CHUNK + c->pat->len);
    if (!chunk) return 0;

    for (off = 0; off < size; off += CS2_SCAN_CHUNK) {
        u32 want = size - off;
        if (want > CS2_SCAN_CHUNK) want = CS2_SCAN_CHUNK;
        if (!cs2_read(c->t, va + off, chunk, want)) break;

        /* 鍏堟妸涓婁竴 chunk 鐨勫熬宸存嫾鍒板紑澶? 澶勭悊璺ㄧ晫鍛戒腑 */
        if (c->tailLen) {
            u8 join[CS2_SIG_MAX_BYTES * 2];
            u32 joinLen = c->tailLen;
            memcpy(join, c->tail, c->tailLen);
            if (want + joinLen <= sizeof(join)) {
                memcpy(join + joinLen, chunk, want);
                joinLen += want;
                scan_buffer(join, joinLen, c->pat, c->tailVa, c->maxHits, c->out);
            }
        }
        scan_buffer(chunk, want, c->pat, va + off, c->maxHits, c->out);

        /* 瀛樺熬宸?*/
        c->tailLen = c->pat->len - 1;
        if (c->tailLen > CS2_SIG_MAX_BYTES) c->tailLen = CS2_SIG_MAX_BYTES;
        if (want >= c->tailLen) {
            memcpy(c->tail, chunk + want - c->tailLen, c->tailLen);
            c->tailVa = va + off + want - c->tailLen;
        } else {
            c->tailLen = 0;
        }

        if (c->out->count >= c->maxHits) break;
    }
    /* the chunk holds a verbatim copy of game module pages */
    cs2_free_wipe(chunk, CS2_SCAN_CHUNK + c->pat->len);
    return c->out->count >= c->maxHits;
}

int cs2_sig_scan(cs2_target* t, u64 moduleBase, u32 moduleSize,
                 const cs2_pattern* pat, int maxHits, cs2_sig_result* out) {
    cs2_module mod;
    if (!pat || !pat->valid || !out) return CS2_FAIL;
    if (maxHits > CS2_SIG_MAX_HITS) maxHits = CS2_SIG_MAX_HITS;

    RtlZeroMemory(out, sizeof(*out));
    mod.base = moduleBase; mod.size = moduleSize; mod.entry = 0;
    mod.timestamp = 0; mod.sections = 0;

    /* 鍏堣瘯"鍙壂鍙墽琛岃妭" */
    {
        scan_ctx c;
        RtlZeroMemory(&c, sizeof(c));
        c.t = t; c.pat = pat; c.maxHits = maxHits; c.out = out;
        c.textOnly = 1; c.tailLen = 0;
        if (walk_sections(t, &mod, cb_scan, &c) && out->count > 0) return CS2_OK;
    }
    /* 鍐嶆壂鍏ㄩ儴鑺?(鏈変簺"鍏ㄥ眬鍙橀噺"鐨勭壒寰佺爜钀藉湪 .data/.rdata 鐨勫紩鐢ㄩ噷) */
    {
        scan_ctx c;
        RtlZeroMemory(&c, sizeof(c));
        c.t = t; c.pat = pat; c.maxHits = maxHits; c.out = out;
        c.textOnly = 0; c.tailLen = 0;
        walk_sections(t, &mod, cb_scan, &c);
    }
    return out->count > 0 ? CS2_OK : CS2_FAIL;
}

int cs2_sig_scan_text(cs2_target* t, const cs2_module* mod,
                      const cs2_pattern* pat, int maxHits, cs2_sig_result* out) {
    scan_ctx c;
    if (!pat || !pat->valid || !out) return CS2_FAIL;
    RtlZeroMemory(out, sizeof(*out));
    RtlZeroMemory(&c, sizeof(c));
    c.t = t; c.pat = pat; c.maxHits = maxHits; c.out = out; c.textOnly = 1; c.tailLen = 0;
    walk_sections(t, mod, cb_scan, &c);
    return out->count > 0 ? CS2_OK : CS2_FAIL;
}

int cs2_sig_rel32(cs2_target* t, u64 hit, u32 dispOffset, u32 instrLen, u64* out) {
    i32 disp;
    if (!cs2_read(t, hit + dispOffset, &disp, sizeof(disp))) return CS2_FAIL;
    *out = hit + instrLen + (i64)disp;
    return CS2_OK;
}

int cs2_sig_resolve(cs2_target* t, const cs2_module* mod, const char* patternText,
                    u32 dispOffset, u32 instrLen, u32 extraDerefs, u32 derefSkip,
                    u64* out, u64* hitAddressOut) {
    cs2_pattern pat;
    cs2_sig_result res;
    u64 addr = 0;
    u32 d;

    if (!cs2_pattern_compile(patternText, &pat)) return CS2_FAIL;
    if (!cs2_sig_scan_text(t, mod, &pat, 1, &res)) return CS2_FAIL;
    if (!res.count) return CS2_FAIL;

    if (hitAddressOut) *hitAddressOut = res.hits[0].address;

    if (dispOffset == 0xFFFFFFFFu) {       /* 鐗瑰緛鐮佹湰韬氨鍛戒腑浜嗘暟鎹湴鍧€ */
        addr = res.hits[0].address;
    } else {
        /* 鏈夊嚑涓寚浠や笉鏄?RIP 鐩稿(渚嬪 mov rax, [rip+disp] 涔嬪悗鍐?add),
         * 璋冪敤鏂圭敤 dispOffset/instrLen 鎻忚堪, 杩欓噷涓嶅仛鐚滄祴銆?*/
        if (!cs2_sig_rel32(t, res.hits[0].address, dispOffset, instrLen, &addr))
            return CS2_FAIL;
    }

    for (d = 0; d < extraDerefs; d++) {
        u64 next = 0;
        if (!cs2_read_ptr(t, addr + derefSkip, &next)) return CS2_FAIL;
        addr = next;
        if (!addr) return CS2_FAIL;
    }
    *out = addr;
    return CS2_OK;
}
