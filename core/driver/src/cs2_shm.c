/* ============================================================================
 * cs2_shm.c -- shared-memory frame channel (replaces the file drop)
 *
 * Model
 *   driver: create a named section once, map it into the system process, and
 *           publish every sampled frame into that view with a seqlock
 *   client: map the same section read-only and take a snapshot; it never opens
 *           cs2.exe, never sends an IOCTL, never touches the disk
 *
 * Why a section instead of a device + IOCTL
 *   A device object lives in \Device and is enumerable in one pass; a driver
 *   object shows up under \Driver. A section is a plain memory object with no
 *   dispatch table, no IRP path and no service entry.
 *
 * Name
 *   GUID-shaped and derived from the target pid, so the name changes with the
 *   session and carries no product string:
 *     \BaseNamedObjects\{A8-B4-C4-D4-E8+pid4}
 *   The client computes the same name from the pid it sees in the process list.
 *   pid 0 is used before a target exists so the channel can be verified while
 *   the game is closed.
 *
 * Lifetime
 *   The section exists only while the driver holds a mapping. DriverUnload
 *   unmaps and closes it; nothing is left on disk and no registry key is used.
 * ========================================================================== */
#include "../inc/cs2core_km.h"
#include "cs2_shm.h"

static HANDLE  g_sec;
static PVOID   g_view;
static SIZE_T  g_view_size;
static u32     g_pid;
static u32     g_size;
static WCHAR   g_name[96];

/* Channel generation. Bumped only when the wire format changes or when two
 * driver instances must not share one section on the same machine. The client
 * tries the current value first and then the previous one. */
#define CS2_SHM_SALT 5u

/* ASCII-independent hex writer: no CRT formatting in kernel. */
static WCHAR* put_hex(WCHAR* p, u32 v, int digits)
{
    static const WCHAR hx[] = L"0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0; i--) { p[i] = hx[v & 0xFu]; v >>= 4; }
    return p + digits;
}

/* Must match FrameShm.cpp::BuildName byte for byte. */
static void build_name(u32 pid, WCHAR* out)
{
    const WCHAR* pre = L"\\BaseNamedObjects\\{";
    WCHAR* p = out;
    u32 a, b, c, d, e;
    int i;

    for (i = 0; pre[i] != 0; i++) { out[i] = pre[i]; }
    p = out + i;

    a = (0x4C325343u ^ pid) ^ (CS2_SHM_SALT << 24);
    b = (0x9E3779B9u * (pid + 1u)) >> 16;
    c = ((pid << 16) ^ 0x5F3Au) & 0xFFFFu;
    d = ((pid >> 3) ^ 0xA1B2C3D4u) & 0xFFFFu;
    e = 0x7F4A7C15u + pid;

    p = put_hex(p, a, 8); *p++ = L'-';
    p = put_hex(p, b & 0xFFFFu, 4); *p++ = L'-';
    p = put_hex(p, c, 4); *p++ = L'-';
    p = put_hex(p, d, 4); *p++ = L'-';
    p = put_hex(p, e, 8);
    p = put_hex(p, pid & 0xFFFFu, 4);
    *p++ = L'}';
    *p = 0;
}

static NTSTATUS shm_create(u32 pid, u32 len)
{
    UNICODE_STRING     name;
    OBJECT_ATTRIBUTES  oa;
    LARGE_INTEGER      max_size;
    SIZE_T             view_size;
    NTSTATUS           st;
    u32                want;

    /* Always map the full v7 frame: the producer may publish a short
     * diagnostic frame, but the consumer always reads a fixed 13968 bytes. */
    want = (len < CS2_FRAME_SIZE_V3) ? (u32)CS2_FRAME_SIZE_V3 : len;

    build_name(pid, g_name);
    RtlInitUnicodeString(&name, g_name);
    InitializeObjectAttributes(&oa, &name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    max_size.QuadPart = want;
    view_size = want;

    st = ZwCreateSection(&g_sec, SECTION_ALL_ACCESS, &oa, &max_size,
                         PAGE_READWRITE, SEC_COMMIT, NULL);
    if (st == STATUS_OBJECT_NAME_COLLISION)
    {
        /* A stale object from an earlier instance is still alive. Mark it
         * temporary and drop our reference, so the last handle closes it and
         * the name disappears before the retry. */
        HANDLE old = NULL;
        NTSTATUS st2 = ZwOpenSection(&old, SECTION_ALL_ACCESS, &oa);
        if (NT_SUCCESS(st2))
        {
            ZwMakeTemporaryObject(old);
            ZwClose(old);
        }
        st = ZwCreateSection(&g_sec, SECTION_ALL_ACCESS, &oa, &max_size,
                             PAGE_READWRITE, SEC_COMMIT, NULL);
    }
    if (!NT_SUCCESS(st)) { g_sec = NULL; return st; }

    st = ZwMapViewOfSection(g_sec, (HANDLE)-1, &g_view, 0, 0, NULL, &view_size,
                            ViewUnmap, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(st))
    {
        ZwClose(g_sec);
        g_sec = NULL;
        g_view = NULL;
        return st;
    }

    g_view_size = view_size;
    g_size = want;
    g_pid = pid;
    return STATUS_SUCCESS;
}

NTSTATUS cs2_shm_publish(u32 pid, const void* frame, u32 len)
{
    volatile u32* hdr;
    NTSTATUS st;

    if (frame == NULL || len < CS2_FRAME_HEADER_SZ || len > CS2_FRAME_SIZE_V3)
    { return STATUS_INVALID_PARAMETER; }

    if (g_view == NULL || g_pid != pid || g_size < len)
    {
        cs2_shm_close();
        st = shm_create(pid, len);
        if (!NT_SUCCESS(st)) { return st; }
    }

    /* seqlock: mark the shared copy as "being written", copy everything except
     * the seq word, then publish the producer's even sequence. A reader that
     * sees an odd seq, or a seq that changed between its two reads, drops the
     * snapshot instead of rendering a mix of two frames. */
    hdr = (volatile u32*)g_view;
    hdr[CS2_OFF_F_SEQ / 4] = 0xFFFFFFFFu;
    KeMemoryBarrier();
    /* Short frames (diagnostics) must not leave a previous real frame's tail
     * behind, or the client would parse stale items/grenades. */
    if (len < g_size) { RtlZeroMemory((u8*)g_view + len, g_size - len); }
    /* Header stays plaintext (magic / version / seq / flags) so the reader can
     * run the seqlock check; the body is XOR'd with a pid-derived byte. A memory
     * dump of the section no longer shows player names / positions in clear. */
    {
        const u8 key = (u8)((pid * 2654435761u) >> 24);
        u8* dst = (u8*)g_view;
        const u8* src = (const u8*)frame;
        u32 i;
        RtlCopyMemory(dst, src, 16);
        if (key == 0) {
            RtlCopyMemory(dst + 16, src + 16, len - 16);
        } else {
            for (i = 16; i < len; i++) { dst[i] = (u8)(src[i] ^ key); }
        }
    }
    KeMemoryBarrier();
    hdr[CS2_OFF_F_SEQ / 4] = ((const u32*)frame)[CS2_OFF_F_SEQ / 4];
    return STATUS_SUCCESS;
}

void cs2_shm_close(void)
{
    if (g_view != NULL)
    {
        ZwUnmapViewOfSection((HANDLE)-1, g_view);
        g_view = NULL;
    }
    if (g_sec != NULL)
    {
        ZwClose(g_sec);
        g_sec = NULL;
    }
    g_view_size = 0;
    g_size = 0;
    g_pid = 0;
}

int cs2_shm_active(void)
{
    return g_view != NULL;
}
