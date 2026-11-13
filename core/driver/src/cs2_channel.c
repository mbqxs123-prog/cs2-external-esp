/* ============================================================================
 * cs2_channel.c  --  v1.4: section 通道整体停用 (保留同名 API 的桩)
 *
 * === 为什么整块停掉 ===
 * 旧实现用命名 section 把帧送到 Win+G 组件:
 *   ZwCreateSection(名字形如 _sm0_9b3f / _sm0_9b3f>env / _sm2_dbg)
 *   + 手搓 SDDL 把读权限授给 ALL APPLICATION PACKAGES (S-1-15-2-1)
 *   + 组件侧按 S-1-15-2-<pkg hash> 的 AppContainerNamedObjects 路径回挂
 *
 * 它在 5E / PAC 的检测面上一次性踩了四条:
 *   1. \Sessions\<n>\BaseNamedObjects 里出现"名字风格像系统设施、但不是任何
 *      已注册组件建的"对象 —— 对象枚举 + 属主交叉比对即可定向;
 *   2. MmMapViewInSystemSpace 留下的系统 VA 映射, 在无模块背书的驱动上
 *      是内核对账时的直接异常项;
 *   3. 二进制里必然带 S-1-15-2-1 / S-1-15-2-<hash> / AppContainerNamedObjects
 *      这些明文串, 静态扫盘一扫就是"有人在跟 AppContainer 通信";
 *   4. 一旦组件用 dev-mode 松散注册, 那条 SID 还会把"我们的驱动"和
 *      "那个没签名的包"直接关联起来。
 *
 * 现在的唯一出口是文件 (cs2_file.c): 定长覆盖写 + 密文路径 + HIDDEN|SYSTEM,
 * 内核里只留一个文件句柄, 运行期**零命名对象、零系统 VA 映射**。
 *
 * 本文件保留全部函数签名 (cs2_channel.h 未改), 实现一律返回失败/NULL, 这样
 *   - 采样线程与 DriverEntry 的调用点不用改;
 *   - 编译器能把整条调用链当常量分支消掉, 镜像里连一条相关字符串都不会留下。
 *
 * 真要恢复 section 通道(例如给别的显示端复用), 从 git 历史取回本文件即可。
 * ========================================================================== */
#include "cs2_channel.h"
#include "cs2_env.h"
#include "cs2_lic.h"
#include "../inc/cs2core_km.h"

NTSTATUS cs2_channel_init(u32 session_id)
{
    (void)session_id;
    return CS2_FAIL;                    /* 不再建命名对象 */
}

int cs2_channel_publish(const u8* frame, u32* seq_inout)
{
    (void)frame; (void)seq_inout;
    return CS2_FAIL;
}

int cs2_channel_keepalive(u32* seq_inout)
{
    (void)seq_inout;
    return CS2_FAIL;
}

int cs2_channel_try_attach_ac(u32 session_id)
{
    (void)session_id;
    return 0;
}

u32 cs2_channel_last_pub_ms(void)
{
    return 0;
}

int cs2_channel_ready(void)
{
    return CS2_FAIL;
}

void cs2_channel_idle(u32 now_ms)
{
    (void)now_ms;
}

void* cs2_channel_env_view(void)
{
    return NULL;                        /* 环境报告不再有共享段 */
}

NTSTATUS cs2_channel_env_init(u32 session_id)
{
    (void)session_id;
    return CS2_FAIL;
}

u32 cs2_channel_readers(void)
{
    return 0;
}

NTSTATUS cs2_channel_lic_init(u32 session_id)
{
    (void)session_id;
    return CS2_FAIL;                    /* 授权走内存态校验, 不再有 lic 段 */
}

u32* cs2_channel_dbg(void)
{
    return NULL;                        /* 诊断段已删除 */
}

void cs2_channel_shutdown(void)
{
    /* 没有对象需要回收; lic 也没绑定过视图 */
}
