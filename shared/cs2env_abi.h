/* ============================================================================
 * cs2env_abi.h  --  Stage-A 环境自检报告 ABI (内核 <-> Game Bar 组件)
 *
 * 目的: 回答两个问题
 *   1) 现在这台机器还能不能干净地进完美(PAC) / 5E —— 平台自己的驱动、服务、
 *      Secure Boot / HVCI / DSE / 测试签名 这些"门槛项"是否就绪;
 *   2) 机器上有没有"有威胁的残留" —— 反作弊驱动/服务残留、第三方 BYOVD 驱动、
 *      映射器痕迹、以及我们自己的痕迹(服务键、驱动对象、磁盘副本、命名对象)。
 *
 * 报告是"按需生成"的: 组件在面板里点"检测环境" → 写 reqOp=1 → 内核线程扫一遍
 * → 回填同一块内存 → 组件读。所以这块 section 是【可写】的(只对 ALL APPLICATION
 * PACKAGES 开放 SECTION_MAP_READ|SECTION_MAP_WRITE), 与帧通道那块只读的区分开。
 *
 * 约定(和帧缓存一样):
 *   - 固定布局, 只追加不移动; 两端各自 static_assert / AssertLayout()。
 *   - seqlock: 内核写时 seq 变奇数, 写完变偶数; 组件读到奇数就重读。
 *   - 字符串一律 UTF-8, 超长截断。
 *   - 只读快照, 不做任何"自动修复"; 清理必须由用户在组件里勾选后显式提交。
 * ========================================================================== */
#ifndef CS2ENV_ABI_H
#define CS2ENV_ABI_H

#define CS2ENV_MAGIC        0x4E453243u   /* 'C','2','E','N' little-endian */
#define CS2ENV_VERSION      1u
#define CS2ENV_MAX_ITEMS    48
#define CS2ENV_TITLE_LEN    48
#define CS2ENV_DETAIL_LEN   160

/* ---------------- 报告头 (64 字节) ---------------- */
#define CS2ENV_OFF_MAGIC         0u   /* u32 */
#define CS2ENV_OFF_VERSION       4u   /* u32 */
#define CS2ENV_OFF_SEQ           8u   /* u32  seqlock */
#define CS2ENV_OFF_STATE        12u   /* u32  0=idle 1=scanning 2=done 3=cleaning */
#define CS2ENV_OFF_REQOP        16u   /* u32  组件写: 0=none 1=scan 2=clean 3=clean+rescan */
#define CS2ENV_OFF_REQSEQ       20u   /* u32  组件自增, 内核回填到 ackSeq */
#define CS2ENV_OFF_ACKSEQ       24u   /* u32 */
#define CS2ENV_OFF_ITEMCOUNT    28u   /* u32 */
#define CS2ENV_OFF_RISK         32u   /* u32  0..100, 越高越危险 */
#define CS2ENV_OFF_CLEANABLE    36u   /* u32  可清理项数量 */
#define CS2ENV_OFF_SCANTICKMS   40u   /* u64  GetTickCount64 采样时刻 */
#define CS2ENV_OFF_FLAGS        48u   /* u32  bit0=扫描完成 bit1=需要重启 bit2=已执行过清理
                                       *      bit4=采样线程摸到 cs2.exe
                                       *      bit5=偏移校验通过
                                       *      bit6=发布过真帧
                                       *      bit7=最近有读失败 */
#define CS2ENV_FLAG_DIAG_GAME    (1u << 4)
#define CS2ENV_FLAG_DIAG_OFFSET  (1u << 5)
#define CS2ENV_FLAG_DIAG_FRAMES  (1u << 6)
#define CS2ENV_FLAG_DIAG_FAILING (1u << 7)
#define CS2ENV_FLAG_DIAG_MASK    (CS2ENV_FLAG_DIAG_GAME | CS2ENV_FLAG_DIAG_OFFSET | \
                                  CS2ENV_FLAG_DIAG_FRAMES | CS2ENV_FLAG_DIAG_FAILING)
#define CS2ENV_OFF_PACREADY     52u   /* u32  0=未知 1=就绪 2=有阻碍 */
#define CS2ENV_OFF_FIVEREADY    56u   /* u32  同上 (5E) */
#define CS2ENV_OFF_DRIVERTICK   60u   /* u32  内核本轮提交时刻 (GetTickCount64 低 32 位) */
#define CS2ENV_HEADER_SIZE      64u

/* ---------------- 报告项 (220 字节) ---------------- */
#define CS2ENV_OFF_I_ID          0u   /* u32  稳定 ID, 见下方 CS2ENV_ID_* */
#define CS2ENV_OFF_I_CATEGORY    4u   /* u8   1=平台就绪 2=AC冲突 3=我方痕迹 4=系统完整性 5=内核风险 */
#define CS2ENV_OFF_I_SEVERITY    5u   /* u8   1=info 2=warn 3=critical */
#define CS2ENV_OFF_I_STATE       6u   /* u8   0=未知 1=正常 2=有风险 3=已修复 4=不适用 */
#define CS2ENV_OFF_I_CLEANABLE   7u   /* u8   0=不能清 1=可清 2=可清但需重启 */
#define CS2ENV_OFF_I_SELECT      8u   /* u8   组件 -> 内核: 1=用户勾选了这一项 */
#define CS2ENV_OFF_I_RESULT      9u   /* u8   内核 -> 组件: 0=无 1=已清理 2=失败 3=跳过 */
#define CS2ENV_OFF_I_RESERVED   10u   /* u16 */
#define CS2ENV_OFF_I_TITLE      12u   /* char[48] UTF-8 */
#define CS2ENV_OFF_I_DETAIL     60u   /* char[160] UTF-8, 清理动作需要的话把命令写在里面 */
#define CS2ENV_ITEM_SIZE       220u

#define CS2ENV_REPORT_SIZE (CS2ENV_HEADER_SIZE + CS2ENV_MAX_ITEMS * CS2ENV_ITEM_SIZE) /* 10624 */

/* ---------------- 分类 ---------------- */
#define CS2ENV_CAT_PLATFORM   1u
#define CS2ENV_CAT_ACCONFLICT 2u
#define CS2ENV_CAT_OURTRACES  3u
#define CS2ENV_CAT_INTEGRITY  4u
#define CS2ENV_CAT_KERNELRISK 5u

/* ---------------- 稳定 ID (组件按 ID 做本地化/分组, 不要按顺序索引) ---------------- */
#define CS2ENV_ID_SECUREBOOT       1u
#define CS2ENV_ID_HVCI             2u
#define CS2ENV_ID_TESTSIGNING      3u
#define CS2ENV_ID_KERNELDEBUG      4u
#define CS2ENV_ID_OSVERSION        5u
#define CS2ENV_ID_HYPERVISOR       6u
#define CS2ENV_ID_DSE              7u
#define CS2ENV_ID_TPM              8u
/* 16..19 来自参考产品(网页雷达)的环境检测项: 它把"平台/ACE 没关干净"当成硬门槛 */
#define CS2ENV_ID_ACE_TRAY        16u   /* ACE-Tray.exe / ACE-Guard / SGuard 未关闭 */
#define CS2ENV_ID_ACE_DRIVER      17u   /* ACE 内核驱动已加载 */
#define CS2ENV_ID_PLATFORM_PROC   18u   /* 完美/5E/FACEIT/宙斯 等平台进程仍在运行 */
#define CS2ENV_ID_TAMPER_BLOCKER  19u   /* 会阻塞驱动加载的第三方 AC (EAC/BE/FACEIT 等) */
#define CS2ENV_ID_AC_DRIVER_VGK    10u
#define CS2ENV_ID_AC_DRIVER_EAC    11u
#define CS2ENV_ID_AC_DRIVER_BE     12u
#define CS2ENV_ID_AC_SERVICE_PAC   13u
#define CS2ENV_ID_AC_SERVICE_5E    14u
#define CS2ENV_ID_AC_RESIDUE       15u
#define CS2ENV_ID_VULN_DRIVER      20u
#define CS2ENV_ID_MAPPER_PROC      21u
#define CS2ENV_ID_MAPPER_FILE      22u
#define CS2ENV_ID_SUSPECT_SVC      23u
#define CS2ENV_ID_OUR_DRIVER       30u
#define CS2ENV_ID_OUR_SERVICE      31u
#define CS2ENV_ID_OUR_FILES        32u
#define CS2ENV_ID_OUR_OBJECTS      33u
#define CS2ENV_ID_OUR_LOGS         34u
#define CS2ENV_ID_UNBACKED_CODE    40u
#define CS2ENV_ID_UNSIGNED_MODULE  41u
#define CS2ENV_ID_NEW_DRIVERS      42u
#define CS2ENV_ID_BIGPOOL_TAG      43u
#define CS2ENV_ID_OVERLAY_CAPTURE  44u
#define CS2ENV_ID_RUN_KEYS         45u
#define CS2ENV_ID_TASKS            46u
#define CS2ENV_ID_WINDBG_KMRD      47u

/* ---------------- 平台就绪度取值 ---------------- */
#define CS2ENV_READY_UNKNOWN  0u
#define CS2ENV_READY_OK       1u
#define CS2ENV_READY_BLOCKED  2u

/* ---------------- 通道名(默认; 实际由内核运行时随机化后写进这里替换) --------------
 * 组件默认读 CS2ENV_CHANNEL_DEFAULT, 与帧通道同前缀但后缀不同:
 *   帧通道: Local\_sm0_9b3f         (只读)
 *   环境报告: Local\_sm0_9b3f>env   (可读可写, 只对 AC 开放)                       */
#define CS2ENV_CHANNEL_DEFAULT "Local\\_sm0_9b3f>env"

/* ---------------- 状态 / 级别 / 动作 枚举 (内核与组件共用同一套名字) ----------------
 * 注意两套 "state" 语义不同但名字不重叠:
 *   报告头 STATE  : IDLE / SCANNING / DONE / CLEANING
 *   单个报告项    : UNKNOWN / OK / RISK / FIXED / NA
 * ----------------------------------------------------------------------------- */
#define CS2ENV_STATE_IDLE       0u   /* 报告头 */
#define CS2ENV_STATE_SCANNING   1u
#define CS2ENV_STATE_DONE       2u
#define CS2ENV_STATE_CLEANING   3u

#define CS2ENV_STATE_UNKNOWN    0u   /* 报告项 */
#define CS2ENV_STATE_OK         1u
#define CS2ENV_STATE_RISK       2u
#define CS2ENV_STATE_FIXED      3u
#define CS2ENV_STATE_NA         4u

#define CS2ENV_SEV_INFO         1u
#define CS2ENV_SEV_WARN         2u
#define CS2ENV_SEV_CRITICAL     3u

#define CS2ENV_CLEAN_NO         0u
#define CS2ENV_CLEAN_YES        1u
#define CS2ENV_CLEAN_REBOOT     2u

#define CS2ENV_REQ_NONE         0u   /* 组件 -> 内核 */
#define CS2ENV_REQ_SCAN         1u
#define CS2ENV_REQ_CLEAN        2u
#define CS2ENV_REQ_CLEAN_RESCAN 3u

#define CS2ENV_RES_NONE         0u   /* 内核 -> 组件 */
#define CS2ENV_RES_DONE         1u
#define CS2ENV_RES_FAIL         2u
#define CS2ENV_RES_SKIP         3u

#define CS2ENV_FLAG_SCANNED     0x1u /* 报告头 FLAGS */
#define CS2ENV_FLAG_REBOOT      0x2u
#define CS2ENV_FLAG_CLEANED     0x4u

/* 编译期自检(两端都要过) */
#if defined(_MSC_VER)
  #define CS2ENV_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
  #define CS2ENV_STATIC_ASSERT(cond, msg) typedef char cs2env_static_assert_##__LINE__[(cond) ? 1 : -1]
#endif

CS2ENV_STATIC_ASSERT(CS2ENV_HEADER_SIZE == 64, "header must stay 64 bytes");
CS2ENV_STATIC_ASSERT(CS2ENV_ITEM_SIZE == 220, "item must stay 220 bytes");
CS2ENV_STATIC_ASSERT(CS2ENV_REPORT_SIZE == 10624, "report size drifted");

#endif /* CS2ENV_ABI_H */
