/* ============================================================================
 * cs2_main.c  --  DriverEntry: 閻熶礁鎳橀崢?Stage-A
 *
 * 闁告凹鍨版慨鈺傘亜閸濆嫮纰?(濞?闁哄牃鍋撳☉鎾崇Т婵傛牠宕鍥厙缂?闁?闁哄牃鍋撶憸鏉垮船閹?):
 *   闁?鐎点倛妗ㄧ悮閬嶅级?section (閻㈩垎鍥ｅ亾濮樻湹澹?+ 闁绘粠鍨伴。銊╁箮閵夈儲鍟為梺顐ｅ哺娴?, 鐎点倛娅ｅ▓鎴﹀籍鐠鸿　鍋撳▎蹇旂殤闁?ALL APPLICATION PACKAGES 閻? *   闁?闁告帗绻傞～鎰板礌閺嶎偄绠氬褍鍟抽崵婊兾涢埀?(闁告瑯浜ｉ鏉库枖閵娿儱鏂€閻?闁哄鐭俊鍥熼垾铏仴, 濞戞挸绉崇粭鍛村箥?
 *   闁?闁告凹鍨版慨鈺呮煂閸ャ劎澹夌紒鎹愭硶閳?(144Hz; 婵炴挸鎲￠崹娆忊柦閳ュ磭纾婚悘蹇氫含閳规牗娼鍌滄惣鐎? 閺夆晜绋撻埢鍏肩▔閳ь剟宕欓搹鐟扮疀缂佹柨顑呴崺銏ゅ箰閸屾瑧鐟?
 *   闁?闁哄牃鍋撻柛姘凹缁旀潙顫㈤妷锕€顤呴柟鑺ワ耿閹?闁炽儲鏌￠埀?闁告挸绉瑰鐗堢鐠佸磭绉垮☉鎾亾婵縿鍎遍妵鎴犳嫻閵夆晛鍘撮弶鈺傤焾閸忔ê顫㈤敐鍛煑闁告鐡曞ù鍥炊閻愬娉? *
 * 閺夆晜鐟ら柌婊勩仚閸楃偛袟: 婵炲备鍓濆﹢?DeviceObject, 婵炲备鍓濆﹢浣虹箔閿曗偓瑜板潡鏌ч悙顒€澶? 濞戞挸绉甸弫?IOCTL, 濞戞挸绉垫晶锕傚籍閵夈儳绠? 濞戞挸绉甸弫鐐哄礃鐏炶姤绀€閻犲鍟ㄩ埀? * 闁活潿鍔嶉崺娑㈠箑娴ｇ儤绠欏☉鎾崇Т閸╁本绂掔拋宕囩Э闁稿繈鍎辫ぐ? 濞戞梻鍠庡銊モ柦閳╁啯绠掗柛娆樺灟娴滄帞鎮?闁瑰灚鎸哥槐?闁汇劌瀚弫楣冨礄婵犳碍妗ㄩ柕? * ========================================================================== */
#include "../inc/cs2core_km.h"
#include "cs2_sampler.h"
#include "cs2_channel.h"
#include "cs2_env.h"
#include "../../../shared/cs2env_abi.h"
#include "cs2_hide.h"
#include "cs2_file.h"
#include "cs2_shm.h"
#include "cs2_strs.h"
#include "cs2_cave.h"
#include "cs2_ptread.h"

static PDRIVER_OBJECT g_drv;

/* ---- 池标签：一次加载内固定，跨加载变化（见 cs2core_km.h 的说明）----
 * 候选都是 Windows 自己在用的四字 tag（小端常量）：
 *   'Ntfs' 'Ntff' 'File' 'MmSt' 'NtFr' 'Filt'                          */
u32 g_cs2_pool_tag;

u32 cs2_pool_tag_init(void)
{
    static const u32 tags[6] = {
        0x7366744Eu,   /* Ntfs */
        0x6666744Eu,   /* Ntff */
        0x656C6946u,   /* File */
        0x74536D4Du,   /* MmSt */
        0x7246744Eu,   /* NtFr */
        0x74696C46u    /* Filt */
    };
    if (g_cs2_pool_tag == 0) {
        u64 t = KeQueryInterruptTime();
        g_cs2_pool_tag = tags[(u32)((t >> 17) % 6u)];
        if (g_cs2_pool_tag == 0) { g_cs2_pool_tag = tags[0]; }
    }
    return g_cs2_pool_tag;
}

static VOID cs2_unload(PDRIVER_OBJECT drv)
{
    /* 顺序：先停采样（不再有人写帧）-> 再关文件（覆写 0 + 截断 + 释放缓冲）
     * -> 最后收通道。以前 cs2_file_close 没有任何调用点：
     *   * 内核句柄一路泄露到进程退出（手工映射的驱动尤其显眼）；
     *   * 磁盘上留着一份完整的、可解密的最后一帧。 */
    cs2_sampler_stop();
    cs2_shm_close();        /* unmap + drop the section object (name disappears) */
    cs2_file_close();
    cs2_channel_shutdown();
    cs2_pt_dump_diag();   /* diagnostic build only: proves the fast read path ran */
#ifdef CS2_ENABLE_CAVE
    cs2_cave_release();   /* put the module's original padding bytes back */
#endif
    /* last: wipe decoded literals so a freed pool block cannot leak plaintext */
    cs2_strs_wipe();
    (void)drv;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING registry_path)
{
    NTSTATUS st;
    void* env_view;

    (void)registry_path;

    if (drv != NULL) drv->DriverUnload = cs2_unload;
    if (drv != NULL) g_drv = drv;

    /* 闁?闂侇偅宀告禍?     *    閻㈩垎鍥ｅ亾濮樻湹澹曢柛鎺旂帛閸撶増绋夊鍛含閺夆晜鐟╅崳宄邦嚈? 閻庣懓鍟幃鏇㈠箰閸ャ劍鎷卞鐟板船婵? 闁兼澘鏈悥鍫曞箣韫囨梻姊鹃悹褔鏀卞鐢稿籍閸撲線鍏囩紓浣哄枛閸ｉ攱绋夊鍫殙閻庢稒锚濠€?     *    濞寸姾顔婄紞宥囦沪閻愭壆鑹鹃柡鍫墴閳瑰秹宕濋妸褎鐣遍柛娑滄閹洜鈧數顢婇挅鍕Υ閸屾稒鏆柣銏や憾閸ｄ即寮芥搴℃疇缂佸顑呭﹢?缁绢収鍠涢?cs2.exe 闁革负鍔忕粣?+ 闁稿绻掍簺闁告瑯鍨抽弫?     *    + 闁归鍘ч崵顓㈡儑閻旈攱濮?濞戞柨顑呴幃妤€顕? 婵炴挸鎲￠崹娆撴焻閳ь剟宕欓崫鍕€?cs2_channel_idle() 闁逛勘鍊栫敮鈧?闁炽儲鏌￠埀?闁哄棙鎸冲﹢鍓佺玻濡も偓瑜?     *    濞?濡炵懓宕慨鈺呭礉閻樼儤绁伴柛鎺撴緲瀹撳繑娼?缂傚倵鏅濋悡顓㈠箣?婵炴挸鎲￠崹娆撴儑閻斿壊鍔€闁革负鍔忕粣鍥儍閸曨垰浜濇繛鍫濈仛濡炲倿姊?闁?     *    闁绘粠鍨伴。銊╁箮閵夈儲鍟為梺顐ｅ哺娴滅偓绋夊鍛創婵炴挸鎲￠崹娆撳极閻楀牆绁? 濞戞挻姊荤划宥嗙閸洘妗ㄩ柡澶庡劵椤╋箓姊捐箛鏃€顦ч柤宕囨櫕閸?婵☆偀鍋撴繛鏉戭儑楠炲棙鏅?, 濞ｅ洦绻冪€垫棃宕濋悩鐑樼グ闁告鍟跨紓鎾诲Υ?*/
    cs2_channel_env_init(0);
    /* 缂備礁瀚▎銏＄瑹瑜庢晶锕€顕ｉ埀顒傛暜瑜旈埀顒佸哺娴滈箖鎮?"Global\\" 闁告挸绉剁槐?闁告劕鎳忛悧宕団偓鐢殿攰閽栧嫰鎷冮挊澶嬭含 session 0 闁?\BaseNamedObjects);
     * 闁兼眹鍎甸崑鍛村级闄囬惌鍓ф偖?AppContainer 闁? 缂備礁瀚▎銏″濮樺啿娈伴柛鏂诲姂閳ь兘鍋撻柛鎺斿閺嬪啯绂掔捄渚У闂侇偅宀告禍楣冨Υ?*/

    /* 闁?闁绘粠鍨伴。銊╂嚊椤忓拋姊? 闁归攱瀵ф慨銈夊川婵犲喚鍞介悷娆忔濞? 缂佹稑顦辩划宥嗙鐠哄搫绲?REQOP=scan */
    env_view = cs2_channel_env_view();
    if (env_view != NULL) {
        cs2_env_init(env_view, CS2ENV_REPORT_SIZE);
        *(u32*)((u8*)env_view + CS2ENV_OFF_MAGIC) = CS2ENV_MAGIC;
        *(u32*)((u8*)env_view + CS2ENV_OFF_VERSION) = CS2ENV_VERSION;
    }

    /* 闁?闂佹彃娲﹂悧杈╃棯鐠恒劉鏌?*/
    /* 60 Hz 采样：ESP 只需要跟上显示刷新，120 Hz 是双倍的内存访问次数，
     * 每次跨进程读都会 attach/detach 目标地址空间 —— 频率本身就是特征。 */
    st = cs2_sampler_start(60);
    if (!NT_SUCCESS(st)) return st;

#ifdef CS2_ENABLE_CAVE
    /* Diagnostic image only (-Cave): find a padding cave in a signed driver and prove
     * it can be written, executed and restored. Result -> C:\Windows\Temp\cc.log */
    (void)cs2_cave_probe();          /* read-only: does a usable cave exist */
#ifdef CS2_ENABLE_CAVE_CLAIM
    (void)cs2_cave_claim();          /* writes + executes a stub; off unless -CaveClaim */
#endif
#endif

    /* 闁?闁硅姤锕㈤幗? 濞寸姴姘︾换鏍煂鐏炵晫纾诲┑? driverquery / 婵☆垪鈧櫕鍋ラ柡瀣煯婵″洭鏌岀仦鑺ョ殤婵炲备鍓濆﹢渚€骞嬮幋婊勭拨濞?*/
    (void)cs2_hide_self;   /* hiding implemented in cs2_hide.c, see note below */
    /* 瀵ゆ儼绻滈幗姗€鎽? 8 缁夋帒鎮楅悽閬嶅櫚閺嶉鍤庣粙瀣⒔鐞?(cs2_hide_poll)閵?     * 閻滄澘婀弰顖滃嚱閹稿洭鎷￠弨鐟板晸閻?unlink_checked(), 娑撳秴鍟€鐠?RemoveEntryList 閻?     * RtlpCheckListEntry 閺嶏繝鐛? 閹碘偓娴犮儰绗夋导姘晙鐟欙箑褰?0x139 CORRUPT_LIST_ENTRY閵?*/
    if (drv != NULL) cs2_hide_defer(drv);   /* manual-map: no DriverObject, no list entry */


    return STATUS_SUCCESS;
}
