/* ============================================================================
 * cs2_lic.h -- 卡密校验（内核侧唯一的那道闸）
 *
 * 组件只是把服务器返回的授权块原样转发到 section 里，**组件里没有密钥**；
 * 真正判断"这张卡能不能用"的是这里。想伪造授权就必须改这个驱动二进制。
 * ========================================================================== */
#ifndef CS2_LIC_H
#define CS2_LIC_H

#include "../inc/cs2core_km.h"

/* lic section 布局（组件创建、内核只读） */
#define CS2_LIC_MAGIC   0x5343494Cu          /* 'L','I','C','S' */
#define CS2_LIC_VERSION 1u
#define CS2_LIC_SIZE    64u

#define CS2_LIC_OFF_MAGIC   0u   /* u32 */
#define CS2_LIC_OFF_VERSION 4u   /* u32 */
#define CS2_LIC_OFF_STATE   8u   /* u32  1=有效 */
#define CS2_LIC_OFF_EXP    12u   /* u32  unix 秒 */
#define CS2_LIC_OFF_HWID   16u   /* u64  宿主指纹 */
#define CS2_LIC_OFF_MAC    24u   /* u64  mac 前 8 字节, 小端 */
#define CS2_LIC_OFF_SEQ    32u   /* u32  心跳序号, 必须严格递增 */

#define CS2_LIC_OK       1
#define CS2_LIC_NOCHAN   0     /* section 还没建出来 */
#define CS2_LIC_EXPIRED -1
#define CS2_LIC_BADMAC  -2
#define CS2_LIC_NOLEASE -3

/* 绑定 lic section 的视图（由 cs2_channel_lic_init 提供），返回 0 成功 */
int  cs2_lic_attach(void* view);
void cs2_lic_detach(void);

/* 每个采样周期调用：>0 有效；<=0 无效（原因见返回值） */
int  cs2_lic_state(void);

/* 计算用：给外部(自检)使用的 mac */
u64  cs2_lic_mac(u64 hwid, u32 exp, u32 seq);

#endif /* CS2_LIC_H */
