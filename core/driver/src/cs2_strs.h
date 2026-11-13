/* cs2_strs.h -- obfuscated literal accessors (see cs2_strs.c, GENERATED) */
#ifndef CS2_STRS_H
#define CS2_STRS_H

#include "../inc/cs2core_km.h"

const char*  cs2_s_exe_a(void);      /* "cs2.exe"  (ANSI)   */
const WCHAR* cs2_s_exe_w(void);      /* "cs2.exe"  (wide)   */
const char*  cs2_s_client(void);     /* "client.dll"        */
const char*  cs2_s_engine(void);     /* "engine2.dll"       */
const char*  cs2_s_schema(void);     /* "schemasystem.dll"  */
const WCHAR* cs2_s_cache_dir(void);  /* delivery directory  */
const WCHAR* cs2_s_cache_leaf(void); /* delivery file name  */
const WCHAR* cs2_s_frame_path(void); /* <dir>\<leaf>        */

/* Zero every decoded buffer (unload path). */
void         cs2_strs_wipe(void);

#endif /* CS2_STRS_H */