#ifndef CS2_SHM_H
#define CS2_SHM_H
/* Shared-memory frame channel.
 *
 * The driver owns a named section (GUID-shaped name derived from the target
 * pid), maps it into the system process and publishes every frame with a
 * seqlock. The display side only maps the same section read-only: no process
 * handle, no file, no IOCTL, no device object.
 */
#include "../inc/cs2core_km.h"

/* Publish one frame. pid == 0 means "no target yet": a diagnostic frame is
 * published under the pid-0 name so the display side can still prove the
 * channel works before CS2 starts. */
NTSTATUS cs2_shm_publish(u32 pid, const void* frame, u32 len);

/* Unmap and drop the section object (called from DriverUnload). */
void cs2_shm_close(void);

/* 1 when a view is currently mapped. */
int cs2_shm_active(void);

#endif /* CS2_SHM_H */
