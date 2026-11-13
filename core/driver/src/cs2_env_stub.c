/* ============================================================================
 * cs2_env_stub.c  --  v1.4: 环境自检模块在生产构建里的空实现
 *
 * 为什么整块不进镜像:
 *   自检面板本身是个好用的开发工具, 但它的"证据表"就是一份作弊清单:
 *     目标进程名 (ace-tray / 5eplay / pwaac / perfectworld ...)
 *     AC 驱动名  (vgk.sys / easyanticheat*.sys / bedaisy*.sys)
 *     我方残留   (cs2core.sys 在 System32\drivers / Temp / Users\Public 的落点)
 *     平台服务名 (PerfectAntiCheat / 5EProtect / ...)
 *   一个"不属于任何已注册模块"的驱动里出现这些字符串, 静态扫盘/扫镜像时
 *   比驱动本身更致命 —— 它等于自述身份。而且这些表在真实扫描被关掉之后
 *   (cs2_env.c 的 CS2_ENVSCAN_ALLOWED=0) 根本不会被执行, 留着只有风险没有收益。
 *
 * 所以生产构建 (tools\build-driver.ps1 默认) 只编译本桩文件; 需要离线体检时
 * 把构建脚本里的源列表换回 cs2_env.c / cs2_env_util.c / cs2_env_clean.c 即可。
 *
 * 保持的语义:
 *   cs2_env_init      -> 记录"无报告段"(报告段随 section 通道一起停用)
 *   cs2_env_diag_bits -> 丢弃状态位 (采样线程据此判断是否还有报告可写)
 *   cs2_env_scan      -> -1 (未启用)
 *   其余              -> 0
 * ========================================================================== */
#include "cs2_env.h"
#include "../inc/cs2core_km.h"

void cs2_env_init(void* reportView, unsigned long reportSize)
{
    (void)reportView;
    (void)reportSize;
}

void cs2_env_poll(void)
{
}

int cs2_env_scan(int startup)
{
    (void)startup;
    return -1;                      /* 未启用 */
}

int cs2_env_clean_selected(void)
{
    return 0;
}

u32 cs2_env_last_risk(void)
{
    return 0;
}

void cs2_env_diag_bits(u32 set_mask, u32 clear_mask)
{
    (void)set_mask;
    (void)clear_mask;
}

void* cs2_env_report_base(void)
{
    return NULL;
}

void cs2_env_item_add(u32 id, u32 cat, u32 sev, u32 state, u32 cleanable,
                      const char* title, const char* detail)
{
    (void)id; (void)cat; (void)sev; (void)state; (void)cleanable;
    (void)title; (void)detail;
}

void cs2_env_find_add(u32 id, u32 kind, const WCHAR* path, const WCHAR* extra)
{
    (void)id; (void)kind; (void)path; (void)extra;
}

cs2env_finding_t* cs2_env_finding_list(u32* count)
{
    if (count) { *count = 0; }
    return NULL;
}

void cs2_env_set_ready(u32 pac, u32 five)
{
    (void)pac; (void)five;
}

void cs2_env_flag_reboot(void)
{
}

int cs2_env_reg_key_exists(const WCHAR* fullPath)
{
    (void)fullPath;
    return 0;
}

u32 cs2_env_report_risk(void)
{
    return 0;
}
