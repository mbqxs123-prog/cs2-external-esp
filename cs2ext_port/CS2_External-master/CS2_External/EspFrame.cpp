// EspFrame.cpp -- every pixel on screen comes from the driver frame.
//
// Rendering follows the layout the reference project already had
// (Render::Get2DBox / DrawBone / DrawHealthBar, Base_Radar, OS-ImGui) and adds
// the pieces a full ESP needs: armour bar, gear letters, magazine, C4 carrier,
// ground items, live grenades and fire areas.

#include "FrameData.h"
#include "EspFrame.h"
#include "Game.h"
#include "Globals.hpp"
#include "MenuConfig.hpp"
#include "Cheats.h"
#include "Radar/Radar.h"

#include <vector>
#include <chrono>
#include <string>
#include <cmath>

// Render.hpp lives in Cheats.cpp (non-inline definitions), so declare what we call.
namespace Render
{
    ImVec4 Get2DBox(const CEntity& Entity);
    void   DrawBone(const CEntity& Entity, ImColor Color, float Thickness);
    void   DrawHealthBar(DWORD Sign, float MaxHealth, float CurrentHealth,
                         ImVec2 Pos, ImVec2 Size, bool Horizontal);
}

static Base_Radar g_radar;

// ---------------------------------------------------------------- primitives
static void TextShadow(const std::string& s, Vec2 pos, ImColor col, float size, bool center = false)
{
    Gui.StrokeText(s, { pos.x + 1.f, pos.y + 1.f }, ImColor(0, 0, 0, 210), size, center);
    Gui.Text(s, pos, col, size, center);
}

// Team 2 = T (orange), 3 = CT (blue). Unknown team keeps the user's box colour.
static ImColor TeamColor(int team)
{
    if (team == 2) return ImColor(245, 170, 60, 255);
    if (team == 3) return ImColor(90, 170, 255, 255);
    return MenuConfig::BoxColor;
}

static void CornerBox(float x, float y, float w, float h, ImColor col, float th)
{
    const float cw = w * 0.28f;
    const float ch = h * 0.16f;
    // top-left / top-right / bottom-left / bottom-right
    Gui.Line({ x, y }, { x + cw, y }, col, th);
    Gui.Line({ x, y }, { x, y + ch }, col, th);
    Gui.Line({ x + w, y }, { x + w - cw, y }, col, th);
    Gui.Line({ x + w, y }, { x + w, y + ch }, col, th);
    Gui.Line({ x, y + h }, { x + cw, y + h }, col, th);
    Gui.Line({ x, y + h }, { x, y + h - ch }, col, th);
    Gui.Line({ x + w, y + h }, { x + w - cw, y + h }, col, th);
    Gui.Line({ x + w, y + h }, { x + w, y + h - ch }, col, th);
}

// Vertical bar growing from the bottom, used for health and armour.
static void SideBar(float x, float y, float h, float w, float frac, ImColor col)
{
    if (frac <= 0.f) return;
    if (frac > 1.f) frac = 1.f;
    Gui.RectangleFilled({ x, y + h * (1.f - frac) }, { w, h * frac }, ImColor(0, 0, 0, 160));
    Gui.Rectangle({ x, y }, { w, h }, ImColor(0, 0, 0, 220), 1.f);
    Gui.RectangleFilled({ x + 1.f, y + h * (1.f - frac) + 1.f }, { w - 2.f, h * frac - 2.f }, col);
}

// Screen-space radius for a world-space radius at the given origin.
static float WorldRadiusToPixels(CView& view, const Vec3& centre, float worldRadius)
{
    Vec2 a, b;
    if (!view.WorldToScreen(centre, a)) return 0.f;
    Vec3 edge = centre; edge.x += worldRadius;
    if (!view.WorldToScreen(edge, b)) return 0.f;
    float r = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    if (r < 0.f || r > 4000.f) return 0.f;
    return r;
}

// ---------------------------------------------------------------- players
static void DrawPlayer(CEntity& e, CView& view)
{
    auto& bl = e.Pawn.BoneData.BonePosList;
    const bool haveHead = bl.size() > (size_t)BONEINDEX::HEAD && bl[BONEINDEX::HEAD].IsVisible;

    Vec2 feet = e.Pawn.ScreenPos;
    float hx = feet.x, hy = feet.y;
    if (haveHead) { hx = bl[BONEINDEX::HEAD].ScreenPos.x; hy = bl[BONEINDEX::HEAD].ScreenPos.y; }

    float h = (feet.y - hy) * 1.14f;          // head to feet, plus a hair above
    if (h < 4.f) h = 4.f;
    float w = h * 0.56f;
    float x = hx - w * 0.5f;
    float y = hy - h * 0.12f;

    const ImColor col = TeamColor(e.Controller.m_iTeamNum);
    const float hpFrac = (float)e.Pawn.m_iHealth / 100.f;
    const float apFrac = (float)e.Pawn.m_iArmor / 100.f;

    if (MenuConfig::ShowBoxESP)
    {
        if (MenuConfig::BoxType == 1)
            CornerBox(x, y, w, h, col, 1.5f);
        else
        {
            if (MenuConfig::ShowBoxFill)
                Gui.RectangleFilled({ x, y }, { w, h }, ImColor(0, 0, 0, 45));
            Gui.Rectangle({ x, y }, { w, h }, col, 1.4f);
        }
    }

    if (MenuConfig::ShowBoneESP)
        Render::DrawBone(e, MenuConfig::BoneColor, 1.3f);

    if (MenuConfig::ShowHealthBar)
        SideBar(x - 6.f, y, h, 3.f, hpFrac,
                ImColor((int)(255 * (1.f - hpFrac)), (int)(255 * hpFrac), 40, 255));

    if (MenuConfig::ShowArmorBar && e.Pawn.m_iArmor > 0)
        SideBar(x + w + 3.f, y, h, 3.f, apFrac, MenuConfig::ArmorColor);

    if (MenuConfig::ShowPlayerName)
        TextShadow(e.Controller.PlayerName, { x + w * 0.5f, y - 15.f }, ImColor(235, 235, 235, 255), 14, true);

    if (MenuConfig::ShowDistance)
    {
        char db[24]; sprintf_s(db, sizeof(db), "%.0fm", e.Pawn.m_flDistance);
        TextShadow(db, { x + w * 0.5f, y + h + 2.f }, ImColor(200, 200, 200, 255), 13, true);
    }

    // weapon + magazine on the bottom line
    if (MenuConfig::ShowWeaponESP)
    {
        char wb[64];
        if (MenuConfig::ShowAmmo && e.Pawn.m_iAmmo > 0)
            sprintf_s(wb, sizeof(wb), "%s [%d]", e.Pawn.WeaponName.c_str(), e.Pawn.m_iAmmo);
        else
            sprintf_s(wb, sizeof(wb), "%s", e.Pawn.WeaponName.c_str());
        TextShadow(wb, { x + w * 0.5f, y + h + 16.f }, ImColor(215, 215, 215, 255), 12, true);
    }

    // gear letters: H helmet, K defuse kit, Z scoped, R reloading, D defusing
    if (MenuConfig::ShowGear)
    {
        std::string g;
        const uint8_t gf = (uint8_t)e.Pawn.m_iGear;
        if (gf & FrameData::GEAR_HELMET)   g += "H";
        if (gf & FrameData::GEAR_DEFUSER)  g += "K";
        if (gf & FrameData::GEAR_ZOOMED)   g += "Z";
        if (gf & FrameData::GEAR_RELOADING)g += "R";
        if (gf & FrameData::GEAR_DEFUSING) g += "D";
        if (!g.empty())
            TextShadow(g, { x + w * 0.5f, y - 30.f }, MenuConfig::GearColor, 12, true);
    }

    // aim direction
    if (MenuConfig::ShowEyeRay)
    {
        Vec3 dir;
        const float p = e.Pawn.ViewAngle.x * 3.14159265f / 180.f;
        const float yw = e.Pawn.ViewAngle.y * 3.14159265f / 180.f;
        dir.x = std::cos(p) * std::cos(yw);
        dir.y = std::cos(p) * std::sin(yw);
        dir.z = -std::sin(p);
        Vec3 end = e.Pawn.m_vOldOrigin;
        end.x += dir.x * 60.f; end.y += dir.y * 60.f; end.z += dir.z * 60.f;
        Vec2 s0, s1;
        if (view.WorldToScreen(e.Pawn.m_vOldOrigin, s0) && view.WorldToScreen(end, s1))
            Gui.Line({ s0.x, s0.y }, { s1.x, s1.y }, ImColor(MenuConfig::EyeRayColor), 1.f);
    }
}

// ---------------------------------------------------------------- items
static const float kUnitsPerMetre = 52.49f;   // CS2: 1 unit = 1.905 cm

static bool ItemCategoryVisible(uint8_t cat)
{
    switch (cat)
    {
    case FrameData::ITEM_WEAPON:  return MenuConfig::ShowItemWeapons;
    case FrameData::ITEM_GRENADE: return MenuConfig::ShowItemGrenades;
    case FrameData::ITEM_C4:      return MenuConfig::ShowItemC4;
    case FrameData::ITEM_KIT:     return MenuConfig::ShowItemKits;
    default:                      return true;
    }
}

static void DrawItems(CView& view, const Vec3& localPos)
{
    const auto& items = FrameData::g_frame.items;
    for (const auto& it : items)
    {
        if (!ItemCategoryVisible(it.cat)) continue;

        float dist = 0.f;
        {
            const float dx = it.pos.x - localPos.x;
            const float dy = it.pos.y - localPos.y;
            const float dz = it.pos.z - localPos.z;
            dist = std::sqrt(dx * dx + dy * dy + dz * dz) / kUnitsPerMetre;
        }
        if (MenuConfig::ItemMaxDistance > 0.f && dist > MenuConfig::ItemMaxDistance) continue;

        Vec2 s;
        if (!view.WorldToScreen(it.pos, s)) continue;
        if (s.x < -50 || s.y < -50 || s.x > 4000 || s.y > 4000) continue;

        ImColor col = MenuConfig::ItemColor;
        std::string label = FrameData::ItemLabel(it.id, it.cat);

        if (it.cat == FrameData::ITEM_C4) col = MenuConfig::C4Color;
        else if (it.cat == FrameData::ITEM_GRENADE) col = MenuConfig::GrenadeColor;
        else if (it.cat == FrameData::ITEM_KIT) col = ImColor(120, 220, 255, 255);

        if (it.flags & FrameData::IF_PLANTED) label = "C4 (planted)";
        else if (it.flags & FrameData::IF_ON_GROUND) label += " (drop)";

        if (MenuConfig::ShowItemDistance)
        {
            char db[32]; sprintf_s(db, sizeof(db), " %.0fm", dist);
            label += db;
        }

        const float bw = MenuConfig::ItemBoxWidth;
        const float bh = bw * 0.46f;
        Gui.Rectangle({ s.x - bw * 0.5f, s.y - bh * 0.5f }, { bw, bh }, col, 1.2f);
        if (MenuConfig::ShowItemAmmo && (it.flags & FrameData::IF_HAS_AMMO) && it.ammo)
        {
            char ab[16]; sprintf_s(ab, sizeof(ab), "%u", (unsigned)it.ammo);
            TextShadow(ab, { s.x, s.y + bh * 0.5f + 3.f }, col, (float)MenuConfig::ItemLabelSize, true);
        }
        TextShadow(label, { s.x, s.y - bh * 0.5f - 12.f }, col, (float)MenuConfig::ItemLabelSize, true);
    }
}

// ---------------------------------------------------------------- grenades
enum GrenClass { GC_OTHER = 0, GC_HE, GC_FLASH, GC_SMOKE, GC_FIRE, GC_DECOY };

static GrenClass GrenClassOf(const FrameData::GrenRec& g)
{
    if (g.kind == FrameData::GREN_KIND_FIRE) return GC_FIRE;
    switch (g.id)
    {
    case 43: return GC_HE;
    case 42: return GC_FLASH;
    case 44: return GC_SMOKE;
    case 45: case 47: return GC_FIRE;      // molotov / incendiary projectile
    case 46: return GC_DECOY;
    default: break;
    }
    return GC_OTHER;
}

static bool GrenClassVisible(GrenClass c)
{
    switch (c)
    {
    case GC_HE:    return MenuConfig::ShowGrenadeHE;
    case GC_FLASH: return MenuConfig::ShowGrenadeFlash;
    case GC_SMOKE: return MenuConfig::ShowGrenadeSmoke;
    case GC_FIRE:  return MenuConfig::ShowGrenadeFire;
    case GC_DECOY: return MenuConfig::ShowGrenadeDecoy;
    default:       return MenuConfig::ShowGrenadeAir;
    }
}

static ImColor GrenClassColor(GrenClass c)
{
    switch (c)
    {
    case GC_HE:    return ImColor(255, 90, 90, 255);
    case GC_FLASH: return ImColor(255, 245, 130, 255);
    case GC_SMOKE: return ImColor(205, 205, 205, 255);
    case GC_FIRE:  return MenuConfig::FireColor;
    case GC_DECOY: return ImColor(150, 255, 150, 255);
    default:       return MenuConfig::GrenadeColor;
    }
}

// Typical effect radius (CS2 units) used when the frame carries no radius.
static float GrenClassRadius(GrenClass c, float fromFrame)
{
    if (fromFrame > 1.f) return fromFrame;
    switch (c)
    {
    case GC_HE:    return 350.f;
    case GC_FLASH: return 200.f;
    case GC_SMOKE: return 144.f;
    case GC_FIRE:  return 150.f;
    case GC_DECOY: return 100.f;
    default:       return 0.f;
    }
}

static void DrawGrenades(CView& view, const Vec3& localPos)
{
    const auto& gs = FrameData::g_frame.grenades;
    for (const auto& g : gs)
    {
        const GrenClass cls = GrenClassOf(g);
        if (!GrenClassVisible(cls)) continue;

        Vec2 s;
        if (!view.WorldToScreen(g.pos, s)) continue;

        const bool fire = (g.kind == FrameData::GREN_KIND_FIRE);
        const ImColor col = GrenClassColor(cls);

        // effect radius ring (blast / burn / smoke)
        float r = 0.f;
        if (MenuConfig::ShowGrenadeRadius)
        {
            const float worldR = GrenClassRadius(cls, g.radius);
            if (worldR > 1.f) r = WorldRadiusToPixels(view, g.pos, worldR);
            if (r > 2.f)
            {
                Gui.Circle({ s.x, s.y }, r, ImColor(col.Value.x, col.Value.y, col.Value.z, 120.f), 1.2f, 48);
                Gui.CircleFilled({ s.x, s.y }, 2.f, col, 8);
            }
        }

        // in-flight marker: always draw a centre dot + velocity tick, even when
        // the frame has no radius for this projectile (r == 0 meant "nothing at
        // all" before, which looked like the ESP had missed it).
        if (!fire)
        {
            Gui.CircleFilled({ s.x, s.y }, 3.f, col, 8);
            Gui.Circle({ s.x, s.y }, 7.f, col, 1.2f, 16);

            Vec3 tip = g.pos;
            tip.x += g.vel.x * 0.15f;
            tip.y += g.vel.y * 0.15f;
            tip.z += g.vel.z * 0.15f;
            Vec2 t2;
            if (view.WorldToScreen(tip, t2))
                Gui.Line({ s.x, s.y }, { t2.x, t2.y }, col, 1.4f);
        }

        // history: throw point -> current position
        if (!fire && MenuConfig::ShowGrenadeAir)
        {
            Vec2 i0;
            if (view.WorldToScreen(g.initPos, i0))
            {
                const float dx = s.x - i0.x, dy = s.y - i0.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                const int seg = (int)(len / 14.f);
                for (int k = 0; k < seg; k += 2)
                {
                    float t0 = (float)k / (float)seg, t1 = (float)(k + 1) / (float)seg;
                    Gui.Line({ i0.x + dx * t0, i0.y + dy * t0 }, { i0.x + dx * t1, i0.y + dy * t1 },
                             ImColor(col.Value.x, col.Value.y, col.Value.z, 90.f), 1.f);
                }
            }
        }

        // prediction: parabola from the current position using velocity + gravity.
        // No map geometry is available, so the arc stops at the first surface
        // crossing only in the sense of "time budget"; it is not a collision trace.
        if (MenuConfig::ShowGrenadeTrajectory && !fire)
        {
            const float dt = 0.05f;
            const float gz = MenuConfig::GrenadeGravity;
            Vec3 prev = g.pos;
            Vec2 prevS{};
            bool prevOk = view.WorldToScreen(prev, prevS);

            for (float t = dt; t <= MenuConfig::GrenadeTrajectoryTime; t += dt)
            {
                Vec3 p = g.pos;
                p.x += g.vel.x * t;
                p.y += g.vel.y * t;
                p.z += g.vel.z * t - 0.5f * gz * t * t;

                Vec2 sp;
                if (view.WorldToScreen(p, sp))
                {
                    if (prevOk)
                        Gui.Line(prevS, sp, ImColor(col.Value.x, col.Value.y, col.Value.z, 110.f), 1.3f);
                    prevS = sp;
                    prevOk = true;
                }
                else
                {
                    prevOk = false;
                }
            }
            // landing hint: small cross at the last projected point
            if (prevOk)
                Gui.CircleFilled({ prevS.x, prevS.y }, 3.f,
                                 ImColor(col.Value.x, col.Value.y, col.Value.z, 200.f), 8);
        }

        // label: type + distance
        const char* name = FrameData::GrenadeName(g.kind, g.id);
        char lb[72];
        const float dx = g.pos.x - localPos.x, dy = g.pos.y - localPos.y, dz = g.pos.z - localPos.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz) / kUnitsPerMetre;
        if (MenuConfig::ShowGrenadeDistance)
            sprintf_s(lb, sizeof(lb), "%s %.0fm", name, dist);
        else
            sprintf_s(lb, sizeof(lb), "%s", name);
        TextShadow(lb, { s.x, s.y - (r > 6.f ? r + 12.f : 14.f) }, col,
                   (float)MenuConfig::GrenadeLabelSize, true);
    }
}

// ---------------------------------------------------------------- main loop
void FrameCheats_Run()
{
    static std::chrono::steady_clock::time_point lastToggle = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    // HOME or INSERT toggles the menu; 150 ms debounce so a held key fires once.
    const bool menuKey = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0 ||
                         (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (menuKey && now - lastToggle >= std::chrono::milliseconds(150))
    {
        MenuConfig::ShowMenu = !MenuConfig::ShowMenu;
        lastToggle = now;
    }
    if (MenuConfig::ShowMenu) Cheats::Menu();

    FrameData::Reload();
    if (!FrameData::Ok()) return;
    FrameData::ApplyView(gGame.View);

    Vec3 localPos{}; float localYaw = 0.f;
    const bool haveLocal = FrameData::LocalOrigin(localPos, localYaw);

    std::vector<CEntity> list;
    FrameData::BuildEntities(list, gGame.View, MenuConfig::ShowTeam);
    for (CEntity& e : list) DrawPlayer(e, gGame.View);

    // C4 carrier tag: the flag lives on the raw record, not on CEntity.
    if (MenuConfig::ShowBombCarrier)
    {
        for (int i = 0; i < FrameData::MAX_SLOTS; i++)
        {
            const auto& p = FrameData::g_frame.players[i];
            if (!(p.flags & FrameData::PF_BOMB)) continue;
            if (p.flags & FrameData::PF_LOCAL) continue;
            CEntity tmp;
            if (!FrameData::FillEntity(i, tmp, gGame.View)) continue;
            TextShadow("C4", { tmp.Pawn.ScreenPos.x, tmp.Pawn.ScreenPos.y - 46.f }, MenuConfig::C4Color, 14, true);
        }
    }

    if (MenuConfig::ShowItems) DrawItems(gGame.View, localPos);
    // Grenades are drawn even when the local player is dead/spectating: the
    // frame still carries projectile positions, only the distance text suffers.
    if (MenuConfig::ShowGrenades) DrawGrenades(gGame.View, localPos);

    if (MenuConfig::ShowSnapline && haveLocal)
    {
        Vec2 ls;
        if (gGame.View.WorldToScreen(localPos, ls))
        {
            for (const auto& e : list)
                if (e.Controller.m_bPawnIsAlive)
                    Gui.Line({ ls.x, ls.y }, { e.Pawn.ScreenPos.x, e.Pawn.ScreenPos.y },
                             ImColor(MenuConfig::SnaplineColor.Value.x, MenuConfig::SnaplineColor.Value.y,
                                     MenuConfig::SnaplineColor.Value.z, 90.f), 1.f);
        }
    }

    if (MenuConfig::ShowRadar && haveLocal)
    {
        ImGui::SetNextWindowBgAlpha(0.10f);
        ImGui::Begin("Radar", 0, ImGuiWindowFlags_NoResize);
        ImGui::SetWindowSize({ MenuConfig::RadarRange * 2.f, MenuConfig::RadarRange * 2.f });

        g_radar.SetDrawList(ImGui::GetWindowDrawList());
        g_radar.SetPos({ ImGui::GetWindowPos().x + MenuConfig::RadarRange,
                         ImGui::GetWindowPos().y + MenuConfig::RadarRange });
        g_radar.SetProportion(MenuConfig::Proportion);
        g_radar.SetRange(MenuConfig::RadarRange);
        g_radar.SetSize(MenuConfig::RadarRange * 2.f);
        g_radar.SetCrossColor(MenuConfig::RadarCrossLineColor);
        g_radar.ShowCrossLine = MenuConfig::ShowRadarCrossLine;
        g_radar.Opened = true;

        for (int i = 0; i < FrameData::MAX_SLOTS; i++)
        {
            const auto& p = FrameData::g_frame.players[i];
            if (p.flags & FrameData::PF_LOCAL) continue;
            if (!(p.flags & FrameData::PF_ALIVE)) continue;
            if (p.flags & FrameData::PF_DORMANT) continue;
            if ((p.flags & FrameData::PF_TEAM) && !MenuConfig::ShowTeam) continue;
            g_radar.AddPoint(localPos, localYaw, p.origin, TeamColor(p.team), MenuConfig::RadarType, p.angles.y);
        }

        // items worth showing on the radar: C4 and dropped weapons are noise,
        // planted C4 is not.
        for (const auto& it : FrameData::g_frame.items)
        {
            if (it.cat != FrameData::ITEM_C4) continue;
            g_radar.AddPoint(localPos, localYaw, it.pos, MenuConfig::C4Color, 0, 0.f);
        }

        g_radar.Render();
        ImGui::End();
    }

    // small status line so a stale frame is obvious at a glance
    if (MenuConfig::ShowLocalInfo && haveLocal)
    {
        char sb[128];
        const float* vm = FrameData::g_frame.view;
        const float rl2 = vm[0] * vm[0] + vm[1] * vm[1] + vm[2] * vm[2];
        sprintf_s(sb, sizeof(sb), "seq %u | players %u | items %u | gren %u | fov %.0f | m0 %.2f rl2 %.2f",
                  FrameData::g_frame.seq, FrameData::g_frame.playerCount,
                  (unsigned)FrameData::g_frame.items.size(),
                  (unsigned)FrameData::g_frame.grenades.size(),
                  FrameData::g_frame.fov, vm[0], rl2);
        TextShadow(sb, { 12.f, 12.f }, ImColor(180, 180, 180, 160), 12, false);
    }
}
