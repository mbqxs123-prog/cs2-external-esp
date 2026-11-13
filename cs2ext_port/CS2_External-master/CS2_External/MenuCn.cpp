#include "MenuCn.h"
#include "Cheats.h"
#include "MenuConfig.hpp"
#include <imgui.h>

namespace MenuCn
{
    static void ColorDot(const char* id, ImColor& col)
    {
        ImGui::SameLine();
        ImGui::ColorEdit4(id, reinterpret_cast<float*>(&col), ImGuiColorEditFlags_NoInputs);
    }

    void Draw()
    {
        static bool init = false;
        if (!init)
        {
            ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.80f;
            init = true;
        }

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Once);
        ImGui::Begin(u8"CS2 透视  (INS 开关)", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        if (ImGui::BeginTabBar(u8"页签"))
        {
            if (ImGui::BeginTabItem(u8"透视"))
            {
                Gui.MyCheckBox(u8"玩家方框", &MenuConfig::ShowBoxESP);
                ColorDot("##box", MenuConfig::BoxColor);
                ImGui::Combo(u8"方框样式", &MenuConfig::BoxType, u8"普通\0四角\0");
                Gui.MyCheckBox(u8"方框填充", &MenuConfig::ShowBoxFill);

                Gui.MyCheckBox(u8"骨骼", &MenuConfig::ShowBoneESP);
                ColorDot("##bone", MenuConfig::BoneColor);

                Gui.MyCheckBox(u8"血条", &MenuConfig::ShowHealthBar);
                Gui.MyCheckBox(u8"护甲条", &MenuConfig::ShowArmorBar);
                ColorDot("##armor", MenuConfig::ArmorColor);

                Gui.MyCheckBox(u8"玩家名字", &MenuConfig::ShowPlayerName);
                Gui.MyCheckBox(u8"距离", &MenuConfig::ShowDistance);
                Gui.MyCheckBox(u8"武器与弹药", &MenuConfig::ShowWeaponESP);
                Gui.MyCheckBox(u8"弹匣数量", &MenuConfig::ShowAmmo);

                Gui.MyCheckBox(u8"装备标记", &MenuConfig::ShowGear);
                Gui.MyCheckBox(u8"C4 携带者", &MenuConfig::ShowBombCarrier);
                ColorDot("##c4", MenuConfig::C4Color);

                Gui.MyCheckBox(u8"地面物品", &MenuConfig::ShowItems);
                if (ImGui::TreeNode(u8"物品选项"))
                {
                    Gui.MyCheckBox(u8"武器", &MenuConfig::ShowItemWeapons);
                    Gui.MyCheckBox(u8"手雷", &MenuConfig::ShowItemGrenades);
                    Gui.MyCheckBox(u8"C4", &MenuConfig::ShowItemC4);
                    Gui.MyCheckBox(u8"拆弹器", &MenuConfig::ShowItemKits);
                    Gui.MyCheckBox(u8"显示距离", &MenuConfig::ShowItemDistance);
                    Gui.MyCheckBox(u8"显示弹匣", &MenuConfig::ShowItemAmmo);
                    float dmin = 0.f, dmax = 100.f;
                    Gui.SliderScalarEx1(u8"最远距离(m)", ImGuiDataType_Float, &MenuConfig::ItemMaxDistance,
                                        &dmin, &dmax, "%.0f", ImGuiSliderFlags_None);
                    float wmin = 16.f, wmax = 48.f;
                    Gui.SliderScalarEx1(u8"图标大小", ImGuiDataType_Float, &MenuConfig::ItemBoxWidth,
                                        &wmin, &wmax, "%.0f", ImGuiSliderFlags_None);
                    int lmin = 8, lmax = 16;
                    Gui.SliderScalarEx1(u8"文字大小", ImGuiDataType_S32, &MenuConfig::ItemLabelSize,
                                        &lmin, &lmax, "%d", ImGuiSliderFlags_None);
                    ImGui::TreePop();
                }

                Gui.MyCheckBox(u8"投掷物", &MenuConfig::ShowGrenades);
                if (ImGui::TreeNode(u8"投掷物选项"))
                {
                    Gui.MyCheckBox(u8"空中飞行", &MenuConfig::ShowGrenadeAir);
                    Gui.MyCheckBox(u8"抛物线预测", &MenuConfig::ShowGrenadeTrajectory);
                    Gui.MyCheckBox(u8"作用范围圈", &MenuConfig::ShowGrenadeRadius);
                    Gui.MyCheckBox(u8"火焰范围", &MenuConfig::ShowGrenadeFire);
                    Gui.MyCheckBox(u8"烟雾范围", &MenuConfig::ShowGrenadeSmoke);
                    Gui.MyCheckBox(u8"闪光范围", &MenuConfig::ShowGrenadeFlash);
                    Gui.MyCheckBox(u8"高爆范围", &MenuConfig::ShowGrenadeHE);
                    Gui.MyCheckBox(u8"诱饵", &MenuConfig::ShowGrenadeDecoy);
                    Gui.MyCheckBox(u8"显示距离", &MenuConfig::ShowGrenadeDistance);
                    float tmin = 0.5f, tmax = 3.0f;
                    Gui.SliderScalarEx1(u8"预测时长(s)", ImGuiDataType_Float, &MenuConfig::GrenadeTrajectoryTime,
                                        &tmin, &tmax, "%.1f", ImGuiSliderFlags_None);
                    float gmin = 400.f, gmax = 1200.f;
                    Gui.SliderScalarEx1(u8"重力", ImGuiDataType_Float, &MenuConfig::GrenadeGravity,
                                        &gmin, &gmax, "%.0f", ImGuiSliderFlags_None);
                    int glmin = 9, glmax = 18;
                    Gui.SliderScalarEx1(u8"文字大小", ImGuiDataType_S32, &MenuConfig::GrenadeLabelSize,
                                        &glmin, &glmax, "%d", ImGuiSliderFlags_None);
                    ImGui::TreePop();
                }

                Gui.MyCheckBox(u8"视线指示", &MenuConfig::ShowEyeRay);
                Gui.MyCheckBox(u8"屏幕连线", &MenuConfig::ShowSnapline);
                Gui.MyCheckBox(u8"显示队友", &MenuConfig::ShowTeam);

                ImGui::Separator();
                ImGui::TextDisabled(u8"INS / HOME 开关菜单");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(u8"雷达"))
            {
                Gui.MyCheckBox(u8"启用雷达", &MenuConfig::ShowRadar);
                ImGui::Combo(u8"点位样式", &MenuConfig::RadarType, u8"圆点\0箭头\0圆点+箭头\0");
                Gui.MyCheckBox(u8"十字线", &MenuConfig::ShowRadarCrossLine);
                ColorDot("##cross", MenuConfig::RadarCrossLineColor);

                float rmin = 100.f, rmax = 300.f;
                Gui.SliderScalarEx1(u8"显示范围", ImGuiDataType_Float, &MenuConfig::RadarRange,
                                    &rmin, &rmax, "%.0f", ImGuiSliderFlags_None);
                float pmin = 500.f, pmax = 3300.f;
                Gui.SliderScalarEx1(u8"缩放比例", ImGuiDataType_Float, &MenuConfig::Proportion,
                                    &pmin, &pmax, "%.0f", ImGuiSliderFlags_None);

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
