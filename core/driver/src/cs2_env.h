/* ============================================================================
 * cs2_env.h  --  环境自检 (Stage-A 侧)
 *
 * 定位: 只读体检 + 用户勾选后的定点清理。不做"自动修复", 不改系统策略,
 *       不下载任何东西。每一项都对应 shared/cs2env_abi.h 里的稳定 ID。
 *
 * 为什么要内核做:
 *   - Game Bar 组件跑在 AppContainer 里, 拿不到注册表/驱动列表/文件时间。
 *   - 内核侧能看到 已加载模块、BigPool 标签、服务键、驱动器目录时间戳 这些
 *     真正能证明"有残留"的证据, 而不是靠猜。
 * ========================================================================== */
#ifndef CS2_ENV_H
#define CS2_ENV_H

#include "../inc/cs2core_km.h"

#define CS2ENV_MAX_FINDINGS 96
#define CS2ENV_PATH_CHARS   260

/* 一条"可操作发现": 扫描时记下来, 用户勾选后由 cs2_env_clean 执行 */
#define CS2ENV_KIND_NONE      0u
#define CS2ENV_KIND_FILE      1u   /* 删文件 */
#define CS2ENV_KIND_DIR       2u   /* 删空目录 */
#define CS2ENV_KIND_SERVICE   3u   /* 停服务 + 删服务键 */
#define CS2ENV_KIND_REGKEY    4u   /* 删注册表键(整个子键) */
#define CS2ENV_KIND_REGSUB    5u   /* 删注册表值 */
#define CS2ENV_KIND_DRIVER    6u   /* ZwUnloadDriver */

typedef struct cs2env_finding
{
    u32  id;                       /* CS2ENV_ID_* */
    u32  kind;                     /* CS2ENV_KIND_* */
    u32  used;
    u32  reserved;
    wchar_t path[CS2ENV_PATH_CHARS];   /* 目标: 文件路径 / 服务名 / 键路径 */
    wchar_t extra[128];                /* 附加: 值名 等 */
} cs2env_finding_t;

/* 由 main 调用: reportView 指向内核里那块环境报告 section 的内核虚拟地址 */
void cs2_env_init(void* reportView, unsigned long reportSize);

/* 采样线程每轮调一次: 处理组件的 scan/clean 请求 (无请求时开销极小) */
void cs2_env_poll(void);

/* 主动扫一遍; startup=1 表示开机自检(结果只写报告不打扰用户) */
int  cs2_env_scan(int startup);

/* 按组件写回的 select 位执行清理; 返回执行的项数 */
int  cs2_env_clean_selected(void);

/* 供 HUD/调试: 最近一次扫描的风险分 */
u32  cs2_env_last_risk(void);

/* 采样线程把自身状态塞进报告头 flags 的高位(不影响扫描结果), 让用户态在
 * "帧通道还没建起来"时也能看出卡在哪一步:
 *   CS2ENV_FLAG_DIAG_GAME / _OFFSET / _FRAMES / _FAILING */
void cs2_env_diag_bits(u32 set_mask, u32 clear_mask);

/* ----------------------------------------------------------------------------
 * 跨文件接口 (cs2_env.c 导出给 cs2_env_util.c / cs2_env_clean.c)
 * -------------------------------------------------------------------------- */
void* cs2_env_report_base(void);                 /* 内核里那块环境报告 section 的视图 */
void  cs2_env_item_add(u32 id, u32 cat, u32 sev, u32 state, u32 cleanable,
                       const char* title, const char* detail);
void  cs2_env_find_add(u32 id, u32 kind, const WCHAR* path, const WCHAR* extra);
cs2env_finding_t* cs2_env_finding_list(u32* count);
void  cs2_env_set_ready(u32 pac, u32 five);      /* CS2ENV_READY_*, 传 UNKNOWN 表示不改 */
void  cs2_env_flag_reboot(void);
int   cs2_env_reg_key_exists(const WCHAR* fullPath);
u32   cs2_env_report_risk(void);

/* ----------------------------------------------------------------------------
 * 文件系统/对象辅助 + 平台门槛检测 (cs2_env_util.c)
 * -------------------------------------------------------------------------- */
int  cs2_file_exists(const WCHAR* ntPath);
int  cs2_section_openable(const WCHAR* name);
int  cs2_scan_dir_for_sys(const WCHAR* ntDir, u32 daysBack, u32 report);
int  str_contains(const char* hay, const char* needle);
void cs2_env_ref_checks(void);        /* ACE/平台进程/阻塞型 AC (参考 01.exe 的检测项) */
void cs2_env_ref_workflow_tip(void);  /* 建议加载顺序提示项 */

#endif /* CS2_ENV_H */
