#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include "ConfigSaver.hpp"
#include "../MenuConfig.hpp" // Include your global settings header

namespace MyConfigSaver {

    // Function to save the configuration to a file
    void SaveConfig(const std::string& filename) {
        std::ofstream configFile(MenuConfig::path+'/'+filename);
        if (!configFile.is_open()) {
            std::cerr << "Error: Could not open the configuration file." << std::endl;
            return;
        }
       
        // Example: Save global settings to the file
        configFile << "ShowBoneESP " << MenuConfig::ShowBoneESP << std::endl;
        configFile << "ShowBoxESP " << MenuConfig::ShowBoxESP << std::endl;
        configFile << "ShowHealthBar " << MenuConfig::ShowHealthBar << std::endl;
        configFile << "FovLineSize " << MenuConfig::FovLineSize << std::endl;
        configFile << "ShowLineToEnemy " << MenuConfig::ShowLineToEnemy << std::endl;
        configFile << "ShowWeaponESP " << MenuConfig::ShowWeaponESP << std::endl;
        configFile << "ShowDistance " << MenuConfig::ShowDistance << std::endl;
        configFile << "ShowFovLine " << MenuConfig::ShowFovLine << std::endl;
        configFile << "ShowEyeRay " << MenuConfig::ShowEyeRay << std::endl;
        configFile << "ShowPlayerName " << MenuConfig::ShowPlayerName << std::endl;
        configFile << "TargetBoneIndex " << MenuConfig::TargetBoneIndex << std::endl;
        configFile << "HealthBarType " << MenuConfig::HealthBarType << std::endl;
        configFile << "BoneColor " << MenuConfig::BoneColor.Value.x << " " << MenuConfig::BoneColor.Value.y << " " << MenuConfig::BoneColor.Value.z << " " << MenuConfig::BoneColor.Value.w << std::endl;
        configFile << "FovLineColor " << MenuConfig::FovLineColor.Value.x << " " << MenuConfig::FovLineColor.Value.y << " " << MenuConfig::FovLineColor.Value.z << " " << MenuConfig::FovLineColor.Value.w << std::endl;
        configFile << "LineToEnemyColor " << MenuConfig::LineToEnemyColor.Value.x << " " << MenuConfig::LineToEnemyColor.Value.y << " " << MenuConfig::LineToEnemyColor.Value.z << " " << MenuConfig::LineToEnemyColor.Value.w << std::endl;
        configFile << "BoxColor " << MenuConfig::BoxColor.Value.x << " " << MenuConfig::BoxColor.Value.y << " " << MenuConfig::BoxColor.Value.z << " " << MenuConfig::BoxColor.Value.w << std::endl;
        configFile << "EyeRayColor " << MenuConfig::EyeRayColor.Value.x << " " << MenuConfig::EyeRayColor.Value.y << " " << MenuConfig::EyeRayColor.Value.z << " " << MenuConfig::EyeRayColor.Value.w << std::endl;
        configFile << "RadarCrossLineColor " << MenuConfig::RadarCrossLineColor.Value.x << " " << MenuConfig::RadarCrossLineColor.Value.y << " " << MenuConfig::RadarCrossLineColor.Value.z << " " << MenuConfig::RadarCrossLineColor.Value.w << std::endl;
        configFile << "HeadShootLineColor " << MenuConfig::HeadShootLineColor.Value.x << " " << MenuConfig::HeadShootLineColor.Value.y << " " << MenuConfig::HeadShootLineColor.Value.z << " " << MenuConfig::HeadShootLineColor.Value.w << std::endl;
        configFile << "ShowMenu " << MenuConfig::ShowMenu << std::endl;
        configFile << "ShowRadar " << MenuConfig::ShowRadar << std::endl;
        configFile << "RadarRange " << MenuConfig::RadarRange << std::endl;
        configFile << "RadarPointSizeProportion " << MenuConfig::RadarPointSizeProportion << std::endl;
        configFile << "ShowCrossLine " << MenuConfig::ShowRadarCrossLine << std::endl;
        configFile << "RadarType " << MenuConfig::RadarType << std::endl;
        configFile << "Proportion " << MenuConfig::Proportion << std::endl;
        configFile << "BoxType " << MenuConfig::BoxType << std::endl;
        configFile << "TeamCheck " << MenuConfig::TeamCheck << std::endl;
		configFile << "VisibleCheck " << MenuConfig::VisibleCheck << std::endl;
        configFile << "ShowHeadShootLine " << MenuConfig::ShowHeadShootLine << std::endl;
        configFile << "ShowCrossHair " << MenuConfig::ShowCrossHair << std::endl;
        configFile << "CrossHairColor " << MenuConfig::CrossHairColor.Value.x << " " << MenuConfig::CrossHairColor.Value.y << " " << MenuConfig::CrossHairColor.Value.z << " " << MenuConfig::CrossHairColor.Value.w << std::endl;
        configFile << "CrossHairSize " << MenuConfig::CrossHairSize << std::endl;
        configFile << "OBSBypass " << MenuConfig::OBSBypass;
        configFile << "BunnyHop " << MenuConfig::BunnyHop;
        configFile << "ShowWhenSpec " << MenuConfig::ShowWhenSpec;
        configFile << "AntiFlashbang " << MenuConfig::AntiFlashbang;
        configFile.close();
        std::cout << "Configuration saved to " << MenuConfig::path + '/' + filename << std::endl;
    }

    // Function to load the configuration from a file
    void LoadConfig(const std::string& filename) {
        std::ifstream configFile(MenuConfig::path + '/' + filename);
        if (!configFile.is_open()) {
            std::cerr << "Error: Could not open the configuration file." << std::endl;
            return;
        }

        std::string line;
        while (std::getline(configFile, line)) {
            std::istringstream iss(line);
            std::string key;
            if (iss >> key) {
                if (key == "ShowBoneESP") iss >> MenuConfig::ShowBoneESP;
                else if (key == "ShowBoxESP") iss >> MenuConfig::ShowBoxESP;
                else if (key == "ShowHealthBar") iss >> MenuConfig::ShowHealthBar;
                else if (key == "FovLineSize") iss >> MenuConfig::FovLineSize;
                else if (key == "ShowLineToEnemy") iss >> MenuConfig::ShowLineToEnemy;
                else if (key == "ShowWeaponESP") iss >> MenuConfig::ShowWeaponESP;
                else if (key == "ShowDistance") iss >> MenuConfig::ShowDistance;
                else if (key == "ShowFovLine") iss >> MenuConfig::ShowFovLine;
                else if (key == "ShowEyeRay") iss >> MenuConfig::ShowEyeRay;
                else if (key == "ShowPlayerName") iss >> MenuConfig::ShowPlayerName;
                else if (key == "TargetBoneIndex") iss >> MenuConfig::TargetBoneIndex;
                else if (key == "HealthBarType") iss >> MenuConfig::HealthBarType;
                else if (key == "BoneColor") iss >> MenuConfig::BoneColor.Value.x >> MenuConfig::BoneColor.Value.y >> MenuConfig::BoneColor.Value.z >> MenuConfig::BoneColor.Value.w;
                else if (key == "FovLineColor") iss >> MenuConfig::FovLineColor.Value.x >> MenuConfig::FovLineColor.Value.y >> MenuConfig::FovLineColor.Value.z >> MenuConfig::FovLineColor.Value.w;
                else if (key == "LineToEnemyColor") iss >> MenuConfig::LineToEnemyColor.Value.x >> MenuConfig::LineToEnemyColor.Value.y >> MenuConfig::LineToEnemyColor.Value.z >> MenuConfig::LineToEnemyColor.Value.w;
                else if (key == "BoxColor") iss >> MenuConfig::BoxColor.Value.x >> MenuConfig::BoxColor.Value.y >> MenuConfig::BoxColor.Value.z >> MenuConfig::BoxColor.Value.w;
                else if (key == "EyeRayColor") iss >> MenuConfig::EyeRayColor.Value.x >> MenuConfig::EyeRayColor.Value.y >> MenuConfig::EyeRayColor.Value.z >> MenuConfig::EyeRayColor.Value.w;
                else if (key == "CrossLineColor") iss >> MenuConfig::RadarCrossLineColor.Value.x >> MenuConfig::RadarCrossLineColor.Value.y >> MenuConfig::RadarCrossLineColor.Value.z >> MenuConfig::RadarCrossLineColor.Value.w;
                else if (key == "HeadShootLineColor") iss >> MenuConfig::HeadShootLineColor.Value.x >> MenuConfig::HeadShootLineColor.Value.y >> MenuConfig::HeadShootLineColor.Value.z >> MenuConfig::HeadShootLineColor.Value.w;
                else if (key == "ShowMenu") iss >> MenuConfig::ShowMenu;
                else if (key == "ShowRadar") iss >> MenuConfig::ShowRadar;
                else if (key == "RadarRange") iss >> MenuConfig::RadarRange;
                else if (key == "RadarPointSizeProportion") iss >> MenuConfig::RadarPointSizeProportion;
                else if (key == "ShowCrossLine") iss >> MenuConfig::ShowRadarCrossLine;
                else if (key == "RadarType") iss >> MenuConfig::RadarType;
                else if (key == "Proportion") iss >> MenuConfig::Proportion;
                else if (key == "BoxType") iss >> MenuConfig::BoxType;
                else if (key == "TeamCheck") iss >> MenuConfig::TeamCheck;
                else if (key == "VisibleCheck") iss >> MenuConfig::VisibleCheck;
                else if (key == "ShowHeadShootLine") iss >> MenuConfig::ShowHeadShootLine;
                else if (key == "ShowCrossHair") iss >> MenuConfig::ShowCrossHair;
                else if (key == "CrossHairColor") iss >> MenuConfig::CrossHairColor.Value.x >> MenuConfig::CrossHairColor.Value.y >> MenuConfig::CrossHairColor.Value.z >> MenuConfig::CrossHairColor.Value.w;
                else if (key == "CrossHairSize") iss >> MenuConfig::CrossHairSize;
                else if (key == "OBSBypass") iss >> MenuConfig::OBSBypass;
                else if (key == "BunnyHop") iss >> MenuConfig::BunnyHop;
                else if (key == "ShowWhenSpec") iss >> MenuConfig::ShowWhenSpec;
                else if (key == "AntiFlashbang") iss >> MenuConfig::AntiFlashbang;
            }
        }

        configFile.close();
        std::cout << "Configuration loaded from " << MenuConfig::path + '/' + filename << std::endl;
    }
} // namespace ConfigSaver
