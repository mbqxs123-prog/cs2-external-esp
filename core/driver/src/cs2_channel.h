/* ============================================================================
 * cs2_channel.h  --  帧发布通道 (内核 -> 组件, 无进程, 无文件)
 *
 * 主通道: 命名 Section。内核 ZwCreateSection 时就挂上显式 DACL, 把
 *         SECTION_MAP_READ 直接授给 ALL APPLICATION PACKAGES (S-1-15-2-1),
 *         所以 Win+G 里的 UWP 组件(AppContainer)不需要任何特权就能读。
 * 备通道: 组件 LocalState 下的文件 (cs2_channel_set_file_fallback)。
 *
 * 会话命名空间注意: 内核线程跑在 system 进程(session 0), 直接建
 * \BaseNamedObjects\x 会落到 session 0, 用户会话(通常 session 1)看不见。
 * 所以这里按目标进程的 session id 拼 \Sessions\<id>\BaseNamedObjects\<name>。
 * ========================================================================== */
#ifndef CS2_CHANNEL_H
#define CS2_CHANNEL_H

#include "../inc/cs2core_km.h"

/* 建/复用帧通道; 进程 session id 由 sampler 传入 (0 = 先用 0 试)。
 * 返回 STATUS_SUCCESS 表示后续可以 publish。 */
NTSTATUS cs2_channel_init(u32 session_id);

/* seqlock 发布: seq 先变奇数 -> 写正文 -> 最后写偶数 seq */
int  cs2_channel_publish(const u8* frame, u32* seq_inout);

/* 空帧保活: 游戏在跑但这一轮没读到真帧时发一个 playerCount=0 的帧,
 * 让 section 与读端映射都活着(否则 CS2_CH_IDLE_MS 后对象会被撤掉)。 */
int  cs2_channel_keepalive(u32* seq_inout);

/* 尝试把发布目标切到组件(AppContainer)自己建的同名 section 上。
 * 返回 1 = 切换成功; 0 = 组件还没建 / 已挂上。采样循环每秒调一次即可。 */
int  cs2_channel_try_attach_ac(u32 session_id);

/* ---- 暴露窗口控制 ----------------------------------------------------------
 * 隐蔽性要求: 系统里"名指本驱动的命名对象"存在的时间越短越好。所以帧 section
 * 不再在 DriverEntry 里建, 而是:
 *   1) 只有在采样线程确认 cs2.exe 真的在跑、并且自动解析已经解出可用偏移之后,
 *      才调 cs2_channel_init() 建;
 *   2) 游戏退出/长时间没有发布, 采样线程调 cs2_channel_idle() 把 section 撤掉
 *      (关掉最后一个句柄 -> 命名对象随之从对象目录消失)。
 * 结果: 游戏不在跑的时候, 对象管理器里没有任何属于本驱动的命名对象,
 *       反作弊枚举 \Sessions\<n>\BaseNamedObjects 也看不到东西。
 * -------------------------------------------------------------------------- */
u32  cs2_channel_last_pub_ms(void);
int  cs2_channel_ready(void);
void cs2_channel_idle(u32 now_ms);

/* 环境报告通道 (可读可写): 返回映射视图, 供 cs2_env_init 用 */
void* cs2_channel_env_view(void);
NTSTATUS cs2_channel_env_init(u32 session_id);

/* 有没有人真的在读 (读端每帧会回写一个心跳计数器; 0 = 没人读) */
u32  cs2_channel_readers(void);

void cs2_channel_shutdown(void);

/* lic section: ??????????, ????????? cs2_lic */
NTSTATUS cs2_channel_lic_init(u32 session_id);
u32*     cs2_channel_dbg(void);

#endif /* CS2_CHANNEL_H */
