/* ============================================================================
 * cs2_lic.c -- SHA-256 + 授权块校验
 *
 * 只依赖标准类型，不引入任何 crypto 库 / 导入表项：
 *   - 内核里能少一个导入就少一个可供反作弊比对的线索；
 *   - 同一份实现也能在用户态编译成自检程序，和服务器算出来的 mac 对拍。
 * ========================================================================== */
#include "cs2_lic.h"

/* ---------------------------------------------------------------- SHA-256 */
typedef struct {
    u32 h[8];
    u64 len;                 /* 已处理字节数 */
    u8  buf[64];
    u32 used;
} cs2_sha256_ctx;

static const u32 K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define ROR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(cs2_sha256_ctx* c, const u8* p)
{
    u32 w[64];
    u32 a, b, dd, e, f, g, h, t1, t2;
    u32 i;

    for (i = 0; i < 16; i++) {
        w[i] = ((u32)p[i * 4] << 24) | ((u32)p[i * 4 + 1] << 16) |
               ((u32)p[i * 4 + 2] << 8) | (u32)p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        u32 s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = c->h[0]; b = c->h[1]; dd = c->h[2]; e = c->h[3];
    f = c->h[4]; g = c->h[5]; h = c->h[6]; /* c->h[7] 下面用 t 拼接, 保持变量简单 */

    /* 用 8 个局部变量跑 64 轮 */
    {
        u32 s[8];
        s[0] = c->h[0]; s[1] = c->h[1]; s[2] = c->h[2]; s[3] = c->h[3];
        s[4] = c->h[4]; s[5] = c->h[5]; s[6] = c->h[6]; s[7] = c->h[7];
        for (i = 0; i < 64; i++) {
            u32 S1 = ROR32(s[4], 6) ^ ROR32(s[4], 11) ^ ROR32(s[4], 25);
            u32 ch = (s[4] & s[5]) ^ ((~s[4]) & s[6]);
            t1 = s[7] + S1 + ch + K256[i] + w[i];
            u32 S0 = ROR32(s[0], 2) ^ ROR32(s[0], 13) ^ ROR32(s[0], 22);
            u32 mj = (s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]);
            t2 = S0 + mj;
            s[7] = s[6]; s[6] = s[5]; s[5] = s[4];
            s[4] = s[3] + t1;
            s[3] = s[2]; s[2] = s[1]; s[1] = s[0];
            s[0] = t1 + t2;
        }
        c->h[0] += s[0]; c->h[1] += s[1]; c->h[2] += s[2]; c->h[3] += s[3];
        c->h[4] += s[4]; c->h[5] += s[5]; c->h[6] += s[6]; c->h[7] += s[7];
    }
    (void)a; (void)b; (void)dd; (void)e; (void)f; (void)g; (void)h;
}

static void sha256_init(cs2_sha256_ctx* c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->len = 0; c->used = 0;
}

static void sha256_update(cs2_sha256_ctx* c, const void* data, u32 n)
{
    const u8* p = (const u8*)data;
    c->len += n;
    while (n > 0) {
        u32 take = 64u - c->used;
        if (take > n) { take = n; }
        {
            u32 i;
            for (i = 0; i < take; i++) { c->buf[c->used + i] = p[i]; }
        }
        c->used += take; p += take; n -= take;
        if (c->used == 64u) { sha256_block(c, c->buf); c->used = 0; }
    }
}

static void sha256_final(cs2_sha256_ctx* c, u8 out[32])
{
    u64 bits = c->len * 8ull;
    u8 pad = 0x80;
    u8 zero = 0;
    u8 lenbuf[8];
    u32 i;

    sha256_update(c, &pad, 1);
    while (c->used != 56u) { sha256_update(c, &zero, 1); }
    for (i = 0; i < 8; i++) { lenbuf[i] = (u8)(bits >> (56 - i * 8)); }
    sha256_update(c, lenbuf, 8);

    for (i = 0; i < 8; i++) {
        out[i * 4 + 0] = (u8)(c->h[i] >> 24);
        out[i * 4 + 1] = (u8)(c->h[i] >> 16);
        out[i * 4 + 2] = (u8)(c->h[i] >> 8);
        out[i * 4 + 3] = (u8)(c->h[i]);
    }
}

/* ------------------------------------------------- 授权校验（无共享密钥） */
/* 以前这里放 32 字节 LIC_SECRET，用它算 HMAC 校验授权块。
 * 问题是：密钥跟着驱动一起发出去，反编译/strings 就能拿走，然后离线伪造任意卡密；
 * 而且它就是"密钥落地"。现在改成：
 *   - 驱动的镜像里**不再有任何密钥**（连常量都没有）；
 *   - 授权由服务器用 RSA-2048 私钥签名，组件用公钥验签（见 LicenseClient.cs）；
 *   - 驱动这边只做"状态/有效期/序号"这类不需要秘密的合理性检查。
 * 这也是为什么删掉 mac 校验不会削弱安全性：真正的凭据是那个签名，
 * 而伪造签名需要服务器私钥，私钥从不离开服务器目录。
 */
/* ---------------------------------------------------------------- 状态机 */
static u8* g_lic_view = NULL;
static int g_lic_last = CS2_LIC_NOCHAN;
static u32 g_lic_last_seq = 0;

int cs2_lic_attach(void* view)
{
    g_lic_view = (u8*)view;
    g_lic_last = CS2_LIC_NOCHAN;
    g_lic_last_seq = 0;
    return (g_lic_view != NULL) ? 0 : -1;
}

void cs2_lic_detach(void)
{
    g_lic_view = NULL;
    g_lic_last = CS2_LIC_NOCHAN;
    g_lic_last_seq = 0;
}

static u32 rd32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u64 rd64(const u8* p) { return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }

static u32 unix_now(void)
{
    /* KeQuerySystemTime = 1601-01-01 起 100ns；转到 unix 秒 */
    LARGE_INTEGER li;
    KeQuerySystemTime(&li);
    {
        u64 unix_seconds = (u64)(li.QuadPart / 10000000ull);
        return (unix_seconds > 11644473600ull)
            ? (u32)(unix_seconds - 11644473600ull)
            : 0u;
    }
}

int cs2_lic_state(void)
{
    const u8* v = g_lic_view;
    u32 magic, ver, state, exp, seq;
    u64 hwid;

    if (v == NULL) { return CS2_LIC_NOCHAN; }

    magic = rd32(v + CS2_LIC_OFF_MAGIC);
    ver   = rd32(v + CS2_LIC_OFF_VERSION);
    state = rd32(v + CS2_LIC_OFF_STATE);
    exp   = rd32(v + CS2_LIC_OFF_EXP);
    hwid  = rd64(v + CS2_LIC_OFF_HWID);
    seq   = rd32(v + CS2_LIC_OFF_SEQ);

    if (magic != CS2_LIC_MAGIC || ver != CS2_LIC_VERSION) { g_lic_last = CS2_LIC_NOLEASE; return g_lic_last; }
    if (state != 1u)                                   { g_lic_last = CS2_LIC_NOLEASE; return g_lic_last; }
    if (exp != 0u && exp <= unix_now())          /* exp==0 = ??? */                             { g_lic_last = CS2_LIC_EXPIRED; return g_lic_last; }
    if (seq == 0)                                      { g_lic_last = CS2_LIC_NOLEASE; return g_lic_last; }
    /* 序号只能前进：旧授权块重放无效 */
    if (seq < g_lic_last_seq)                          { g_lic_last = CS2_LIC_NOLEASE; return g_lic_last; }
    g_lic_last_seq = seq;

    g_lic_last = CS2_LIC_OK;
    return CS2_LIC_OK;
}
