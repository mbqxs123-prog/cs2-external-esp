/* ============================================================================
 * cs2_seed.h  --  Stage-A �?偏移种子" (自动生成, 不要手改)
 *
 * 来源: cs2-dumper 输出 (a2x/cs2-dumper @main), �?tools/fetch-offsets.ps1 抽取�? * 生成时间: 2026-10-15 13:10:38 UTC
 *
 * 定位(重要):
 *   这是**种子 / 兜底�?*, 不是唯一真相。Stage-A 的取数是:
 *     �?运行期用特征码扫�?client.dll / engine2.dll)重新定位 dw* 锚点;
 *     �?netvar 偏移优先按名字走 schema 自解�? 失败才退回本文件的种子�?
 *     �?每个值都要过语义校验(血 0..100 / 队伍 2..3 / 坐标有限且在合理范围 /
 *        viewMatrix 后三行近�?0,0,0,1 ...), 校验不过就当"这版对不�?, 退回扫描器�? *   所以游戏更新后: 先跑 tools/fetch-offsets.ps1 刷新本文�? 或者干脆什么都不做
 *   —�?扫描器会自己找回�? 组件一行都不用改�? * ========================================================================== */
#ifndef CS2_SEED_H
#define CS2_SEED_H

#define CS2_SEED_SOURCE     "cs2-dumper @main (a2x)"
#define CS2_SEED_GENERATED  "2026-11-13 11:06:58 UTC (fetch-offsets.ps1)"

/* ============================ client.dll 锚点 ============================ */
#define CS2_DW_CSGOINPUT                     0x2569E30u
#define CS2_DW_ENTITYLIST                    0x2709010u   /* == dwGameEntitySystem */
#define CS2_DW_GAMEENTITYSYSTEM              0x2709010u
#define CS2_DW_GAMEENTITYSYSTEM_HIGHESTIDX   0x2120u
#define CS2_DW_GAMERULES                     0x25537E8u
#define CS2_DW_GLOBALVARS                    0x2221AB8u
#define CS2_DW_GLOWMANAGER                   0x2550500u
#define CS2_DW_LOCALPLAYERCONTROLLER         0x252B0C8u
#define CS2_DW_LOCALPLAYERPAWN               0x2553D38u
#define CS2_DW_PLANTEDC4                     0x24B9B90u
#define CS2_DW_PREDICTION                    0x2553C40u
#define CS2_DW_SENSITIVITY                   0x2551038u
#define CS2_DW_VIEWANGLES                    0x256A4B8u
#define CS2_DW_VIEWMATRIX                    0x2559640u
#define CS2_DW_VIEWRENDER                    0x255A008u
#define CS2_DW_WEAPONC4                      0x24B83B0u

/* ============================ engine2.dll ================================ */
#define CS2_DW_BUILDNUMBER                   0x6B2D18u   /* 用来识别游戏版本 */
#define CS2_DW_NETWORKGAMECLIENT             0x9AFB30u
#define CS2_DW_NGC_LOCALPLAYER               0xF8u
#define CS2_DW_NGC_MAXCLIENTS                0x240u
#define CS2_DW_NGC_SIGNONSTATE               0x230u
#define CS2_DW_NGC_CLIENTTICKCOUNT           0x398u
#define CS2_DW_NGC_SERVERTICKCOUNT           0x24Cu
#define CS2_DW_WINDOWWIDTH                   0x9B4290u   /* 视口来源之一 */
#define CS2_DW_WINDOWHEIGHT                  0x9B4294u

/* ============================ 其它模块 ================================== */
#define CS2_DW_INPUTSYSTEM                   0x46BC0u   /* inputsystem.dll */
#define CS2_DW_GAMETYPES                     0x1AEFC0u   /* matchmaking.dll */
#define CS2_DW_SOUNDSYSTEM                   0x532470u   /* soundsystem.dll */

/* ======================= 实体列表解引用配�?稳定) ========================
 *   handle = controller->m_hPlayerPawn          (uint32, �?15 位是索引)
 *   list   = *(client + dwEntityList)
 *   chunk  = *(list + 0x8 * (index >> 9) + 0x10)
 *   entity = *(chunk + 0x70 * (index & 0x1FF))
 * 0x8/0x10/0x1FF/>>9 �?2024 年至今没�? **stride 已经变成 0x70 �?*
 * (2026-10 实测 build 14191: 本地 pawn 落在 idx 417, 指针位置 chunk0+0xB670,
 *  0xB670 / 417 = 0x70 整除; local controller �?chunk0+0x70 = idx 1)�? * 之前�?0x78 读出来的全是错位指针, 语义校验(血 0..100 / �?2..3)必然失败,
 * 表现就是"进程找到了但永远不发布帧"�? * ====================================================================== */
#define CS2_ENT_CHUNK_MUL       0x8u
#define CS2_ENT_CHUNK_OFF       0x10u
#define CS2_ENT_HANDLE_MASK     0x7FFFu
#define CS2_ENT_CHUNK_SHIFT     9u
#define CS2_ENT_STRIDE          0x70u
#define CS2_ENT_INDEX_MASK      0x1FFu

/* ===================== netvar / schema 偏移 (类内偏移) ==================
 * 名字里的类名�?schema 里的�? 取数时按继承链累�? 例如 C_BaseEntity::m_iHealth
 * �?C_CSPlayerPawn 上就是同一个偏�?CS2 �?schema 偏移已经�?类内绝对偏移")�? * ====================================================================== */

/* --- C_BaseEntity --- */
#define CS2_NV_M_PGAMESCENENODE              0x330u   /* CGameSceneNode* */
#define CS2_NV_M_IHEALTH                     0x34Cu   /* int32  */
#define CS2_NV_M_IMAXHEALTH                  0x348u   /* int32  */
#define CS2_NV_M_LIFESTATE                   0x354u   /* uint8  (0 = alive) */
#define CS2_NV_M_ITEAMNUM                    0x3E7u   /* uint8  (2 = T, 3 = CT) */
#define CS2_NV_M_FLAGS                       0x3F4u   /* uint32 */
#define CS2_NV_M_VECABSVELOCITY              0x3F8u   /* Vector (量化�? 不准) */
#define CS2_NV_M_VECVELOCITY                 0x430u   /* Vector (给外推用) */
#define CS2_NV_M_HOWNERENTITY                0x520u   /* CHandle */
#define CS2_NV_M_MOVETYPE                    0x525u   /* uint8  */
#define CS2_NV_M_FLSIMULATIONTIME            0x3B8u   /* GameTime_t */

/* --- C_BaseGrenade / C_BaseCSGrenadeProjectile / C_Inferno ---
 * 投掷物透视 + 燃烧范围用 (ABI v7 尾部)。
 * 全部来自 cs2-dumper 的 client_dll.hpp, 不是手推的。 */
#define CS2_NV_M_BISSMOKEGRENADE             0x1269u   /* bool */
#define CS2_NV_M_BISLIVE                    0x126Au   /* bool */
#define CS2_NV_M_DMGRADIUS                  0x126Cu   /* float32 爆炸/燃烧半径(单位) */
#define CS2_NV_M_FLDETONATETIME             0x1270u   /* GameTime_t 起爆时刻 */
#define CS2_NV_M_FLDAMAGE                   0x1278u   /* float32 */
#define CS2_NV_M_VINITIALPOSITION           0x12B0u   /* VectorWS 出手点 */
#define CS2_NV_M_VINITIALVELOCITY           0x12BCu   /* Vector 初速度 */
#define CS2_NV_M_NBOUNCES                   0x12C8u   /* int32 */
#define CS2_NV_M_FLSPAWNTIME                0x12E8u   /* GameTime_t */
#define CS2_NV_M_FIREPOSITIONS              0x1108u   /* VectorWS[64] */
#define CS2_NV_M_BFIREISBURNING             0x1708u   /* bool[64] */
#define CS2_NV_M_FIRECOUNT                  0x1A48u   /* int32 */
#define CS2_NV_M_NINFERNOTYPE               0x1A4Cu   /* int32 */
#define CS2_NV_M_NFIRELIFETIME              0x1A50u   /* float32 燃烧持续(秒) */

/* --- C_BaseModelEntity --- */
#define CS2_NV_M_COLLISION                   0xD30u   /* CCollisionProperty */
#define CS2_NV_M_VECVIEWOFFSET               0xF60u   /* Vector (眼睛相对原点) */

/* --- CCollisionProperty (盒子尺寸真�? �?身高估宽"�? --- */
#define CS2_NV_M_VECMINS                     0x40u   /* Vector */
#define CS2_NV_M_VECMAXS                     0x4Cu   /* Vector */
#define CS2_NV_M_FLBOUNDINGRADIUS            0x60u   /* float32 */

/* --- C_BasePlayerPawn --- */
#define CS2_NV_M_HCONTROLLER                 0x14BCu   /* CHandle<CBasePlayerController> */
#define CS2_NV_M_PWEAPONSERVICES             0x12F0u
#define CS2_NV_M_VOLDORIGIN                  0x14A4u   /* Vector (插值前的位�? 想更跟手可用) */

/* --- C_CSPlayerPawn --- */
#define CS2_NV_M_ANGEYEANGLES                0x3630u   /* QAngle (2 �?float: pitch, yaw) */
#define CS2_NV_M_BISSCOPED                   0x1ED0u   /* bool */
#define CS2_NV_M_BISDEFUSING                 0x1ED2u   /* bool */
#define CS2_NV_M_BISWALKING                  0x1EB0u   /* bool */
#define CS2_NV_M_ENTITYSPOTTEDSTATE          0x1EB8u   /* EntitySpottedState_t */
#define CS2_NV_M_ISHOTSFIRED                 0x1EE8u   /* int32  */
#define CS2_NV_M_ARMORVALUE                  0x1F00u   /* int32  */
#define CS2_NV_M_SZLASTPLACENAME             0x15BCu   /* char[18] */

/* --- C_CSPlayerPawnBase --- */
#define CS2_NV_M_FLFLASHMAXALPHA             0x150Cu   /* float32 (被闪的白色遮�? >0 可用) */
#define CS2_NV_M_FLFLASHDURATION             0x1510u   /* float32 */

/* --- CCSPlayerController / CBasePlayerController --- */
#define CS2_NV_M_HPLAYERPAWN                 0x92Cu   /* CHandle<C_CSPlayerPawn> */
#define CS2_NV_M_BPAWNISALIVE                0x934u   /* bool */
#define CS2_NV_M_IPAWNHEALTH                 0x938u   /* int32 (�?pawn 的血一�? 拿不�?pawn 时用) */
#define CS2_NV_M_BPAWNHASDEFUSER             0x940u   /* bool */
#define CS2_NV_M_BPAWNHASHELMET              0x941u   /* bool */
#define CS2_NV_M_IPING                       0x838u   /* uint32 */
#define CS2_NV_M_ISCORE                      0x954u   /* int32  */
#define CS2_NV_M_BISLOCALPLAYERCTRL          0x790u   /* bool (判断本地玩家的更稳来�? */
#define CS2_NV_M_HOBSERVERPAWN               0x930u
#define CS2_NV_M_SSANITIZEDNAME              0x878u   /* CUtlString (带长�? 要二次解引用) */
#define CS2_NV_M_ISZPLAYERNAME               0x6FCu   /* char[128] �?直接用这个取名字 */
#define CS2_NV_M_NTICKBASE                   0x6B8u   /* uint32 */

/* --- 武器/弹药�?(ABI v5 �?weapon/ammo/armor/gear 就靠这条链填) -----------------
 *   pawn + m_pWeaponServices (0x12F0)          -> CPlayer_WeaponServices
 *   + m_hActiveWeapon  (0x60)                  -> CHandle<C_CSWeaponBase>
 *   weapon + m_AttributeManager (0x1290)       -> C_AttributeContainer   (C_EconEntity)
 *   + m_Item (0x50)                            -> C_EconItemView
 *   + m_iItemDefinitionIndex (0x1BA, uint16)   -> 武器 ID (1=deagle ... �?WeaponTable)
 *   weapon + m_iClip1 (0x1928, int32)          -> 弹匣余弹
 * 注意: 三个类里都有�?m_AttributeManager 的字�?C_Chicken 0x14C8 / C_BaseEntity 0x12E8 /
 * C_EconEntity 0x1290), 武器用的�?C_EconEntity 这个 0x1290�?*/
#define CS2_NV_M_ATTRIBUTEMANAGER            0x1290u   /* C_EconEntity::m_AttributeManager */
#define CS2_NV_M_ITEM                        0x50u   /* C_AttributeContainer::m_Item */
#define CS2_NV_M_ITEMDEFINITIONINDEX         0x1BAu   /* C_EconItemView, uint16 ★武器ID */
#define CS2_NV_M_ICLIP1                      0x1928u   /* C_BasePlayerWeapon, int32 弹药 */
#define CS2_NV_M_BINRELOAD                   0x1A7Cu   /* C_CSWeaponBase, bool (CS2_GEAR_RELOADING) */

/* --- CGameSceneNode (m_pGameSceneNode 指向的就是它; CSkeletonInstance 继承�? --- */
#define CS2_NV_M_POWNER                      0x30u   /* CEntityInstance* (反查实体) */
#define CS2_NV_M_VECORIGIN                   0x80u   /* 局部原�?量化) */
#define CS2_NV_M_VECABSORIGIN                0xC8u   /* �?世界坐标 */
#define CS2_NV_M_ANGABSROTATION              0xD4u   /* QAngle */
#define CS2_NV_M_FLSCALE                     0xC4u
#define CS2_NV_M_BDORMANT                    0x103u   /* bool �?休眠�?*/

/* --- CSkeletonInstance / CModelState / EntitySpottedState_t --- */
#define CS2_NV_M_MODELSTATE                  0x140u   /* CModelState */
#define CS2_NV_M_NHITBOXSET                  0x3FCu
#define CS2_NV_M_HMODEL                      0xA0u   /* CModelState �?*/
#define CS2_NV_M_MODELNAME                   0xA8u   /* CModelState �?*/
#define CS2_NV_ES_M_BSPOTTED                 0x8u   /* EntitySpottedState_t �?*/
#define CS2_NV_ES_M_BSPOTTEDBYMASK           0xCu   /* uint32[2] */

/* ======================= 骨骼数组 (�?schema 字段) =======================
 * 骨骼不在 schema �?不是网络字段), 它是 CModelState �?+0x80 的裸指针:
 *     boneArray = *(pawnSceneNode + m_modelState + 0x80)
 * 每根骨骼是一�?CTransform (0x20 字节), 我们只要里面的位�?Vector�? * CTransform 的排布是 { Quaternion m_quat(16B, 16 对齐); Vector m_vPosition(12B);
 * float m_flScale(4B); }, 所以位置的类内偏移�?0x10�? *
 * �?但历史上有过 0x0 / 0x10 两种读法, 所�?Stage-A 不写�? 它会对同一根骨骼按
 *   两个候选偏移各取一次位�? �?骨骼间距是否落在 1..40 单位、头部是否在胸口之上"
 *   打分, 选得分高的那�?每个�?build 只做一�? 结果缓存在驱动里)�? * ====================================================================== */
#define CS2_BONE_ARRAY_FROM_MODELSTATE  0x80u
#define CS2_BONE_STRIDE                 0x20u
#define CS2_BONE_POS_CANDIDATE_A        0x10u    /* CTransform::m_vPosition */
#define CS2_BONE_POS_CANDIDATE_B        0x00u    /* 备�? 直接�?Vector3 �?*/

/* CS2 原生骨骼索引 -> 我们 ABI 的语义槽�?�?shared/cs2core_abi.h)
 * 原生名字来自 model "cs_player" 的骨骼表; 索引在多版本间稳�? �?Stage-A 仍会
 * �?父子关系 + 相对位置"重新校验一�?头在颈上、手在肘外侧...)�?*/
#define CS2_BONE_IDX_HEAD       6u     /* ʵ��: ��ߵ� z=60(��Խŵ�) */
#define CS2_BONE_IDX_NECK       5u
#define CS2_BONE_IDX_CHEST      4u     /* spine_3: 原来�?23 �?FOOT_R 冲突, 骨骼畸形就出在这 */
#define CS2_BONE_IDX_STOMACH    2u     /* spine_1: 原来�?22 �?KNEE_R 冲突 */
#define CS2_BONE_IDX_PELVIS     0u
#define CS2_BONE_IDX_SHOLDER_L  7u
#define CS2_BONE_IDX_ELBOW_L    9u
#define CS2_BONE_IDX_HAND_L     10u
#define CS2_BONE_IDX_SHOLDER_R  11u
#define CS2_BONE_IDX_ELBOW_R    13u
#define CS2_BONE_IDX_HAND_R     14u
#define CS2_BONE_IDX_HIP_L      16u
#define CS2_BONE_IDX_KNEE_L     17u
#define CS2_BONE_IDX_FOOT_L     18u
#define CS2_BONE_IDX_HIP_R      19u
#define CS2_BONE_IDX_KNEE_R     20u
#define CS2_BONE_IDX_FOOT_R     21u

#endif /* CS2_SEED_H */
