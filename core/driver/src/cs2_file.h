#pragma once
#include "cs2core_km.h"

/* 1 = mirror every frame to the hidden .db file as well as the shared section.
 * Default 0: the file path leaves a fixed-path artifact on disk (a minifilter
 * or a disk sweep can find it), and the section channel already gives the
 * display side a lower-latency copy. Turn it on only for troubleshooting. */
#ifndef CS2_FILE_MIRROR
#define CS2_FILE_MIRROR 0
#endif

/* 帧文件输出 (共享内存的降级备份): 路径来自 cs2_strs.c 的密文表, 属性 HIDDEN|SYSTEM,
 * 写入频率上限 60Hz, 长度恒定为 CS2_FRAME_SIZE_V3。 */
NTSTATUS cs2_file_init(void);
NTSTATUS cs2_file_write(const void* buf, u32 len);
void     cs2_file_close(void);
int      cs2_file_ready(void);
u32      cs2_file_skipped(void);
