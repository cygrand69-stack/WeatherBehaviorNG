#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

Config g_config{};
float g_updateRadiusSquared = g_config.updateRadius * g_config.updateRadius;

namespace {
    std::string Trim(std::string value) {
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };

        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool ParseBool(const std::string& value, bool defaultValue) {
        const auto lowered = ToLower(Trim(value));

        if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
            return true;
        }

        if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
            return false;
        }

        return defaultValue;
    }

    float ParseFloat(const std::string& value, float defaultValue) {
        try {
            return std::stof(Trim(value));
        } catch (...) {
            return defaultValue;
        }
    }

    int ParseInt(const std::string& value, int defaultValue) {
        try {
            return std::stoi(Trim(value));
        } catch (...) {
            return defaultValue;
        }
    }

    void ApplyConfigSanity() {
        if (g_config.updateRadius < 0.0f) {
            g_config.updateRadius = 0.0f;
        }

        if (g_config.weatherTransitionDelay < 0.0f) {
            g_config.weatherTransitionDelay = 0.0f;
        }

        if (g_config.tickerIntervalMs < 50) {
            g_config.tickerIntervalMs = 50;
        }

        if (g_config.interiorUnequipMin < 0.0f) {
            g_config.interiorUnequipMin = 0.0f;
        }
        if (g_config.interiorUnequipMax < 0.0f) {
            g_config.interiorUnequipMax = 0.0f;
        }
        if (g_config.weatherEquipMin < 0.0f) {
            g_config.weatherEquipMin = 0.0f;
        }
        if (g_config.weatherEquipMax < 0.0f) {
            g_config.weatherEquipMax = 0.0f;
        }
        if (g_config.clearUnequipMin < 0.0f) {
            g_config.clearUnequipMin = 0.0f;
        }
        if (g_config.clearUnequipMax < 0.0f) {
            g_config.clearUnequipMax = 0.0f;
        }

        if (g_config.interiorUnequipMax < g_config.interiorUnequipMin) {
            std::swap(g_config.interiorUnequipMin, g_config.interiorUnequipMax);
        }
        if (g_config.weatherEquipMax < g_config.weatherEquipMin) {
            std::swap(g_config.weatherEquipMin, g_config.weatherEquipMax);
        }
        if (g_config.clearUnequipMax < g_config.clearUnequipMin) {
            std::swap(g_config.clearUnequipMin, g_config.clearUnequipMax);
        }
        if (g_config.hoodChancePercent < 0) {
            g_config.hoodChancePercent = 0;
        }
        if (g_config.hoodChancePercent > 100) {
            g_config.hoodChancePercent = 100;
        }

        if (g_config.cloakChancePercent < 0) {
            g_config.cloakChancePercent = 0;
        }
        if (g_config.cloakChancePercent > 100) {
            g_config.cloakChancePercent = 100;
        }
        if (g_config.scarfChancePercent < 0) {
            g_config.scarfChancePercent = 0;
        }
        if (g_config.scarfChancePercent > 100) {
            g_config.scarfChancePercent = 100;
        }
        if (g_config.scarfInsteadOfCloakChancePercent < 0) {
            g_config.scarfInsteadOfCloakChancePercent = 0;
        }
        if (g_config.scarfInsteadOfCloakChancePercent > 100) {
            g_config.scarfInsteadOfCloakChancePercent = 100;
        }
        g_updateRadiusSquared = g_config.updateRadius * g_config.updateRadius;
    }
}

void LoadConfig() {
    g_config = Config{};
    std::ifstream file("Data\\SKSE\\Plugins\\WeatherBehaviorNG.ini");
    if (!file.is_open()) {
        ApplyConfigSanity();
        return;
    }

    std::string line;
    std::string section;

    while (std::getline(file, line)) {
        line = Trim(line);

        if (line.empty()) {
            continue;
        }

        if (line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        const std::string key = ToLower(Trim(line.substr(0, equalsPos)));
        const std::string value = Trim(line.substr(equalsPos + 1));

        if (section == "general") {
            if (key == "fupdateradius") {
                g_config.updateRadius = ParseFloat(value, g_config.updateRadius);
            } else if (key == "fweathertransitiondelay") {
                g_config.weatherTransitionDelay = ParseFloat(value, g_config.weatherTransitionDelay);
            } else if (key == "itickerintervalms") {
                g_config.tickerIntervalMs = ParseInt(value, g_config.tickerIntervalMs);
            } else if (key == "iscarfchancepercent") {
                g_config.scarfChancePercent = ParseInt(value, g_config.scarfChancePercent);
            } else if (key == "ihoodchancepercent") {
                g_config.hoodChancePercent = ParseInt(value, g_config.hoodChancePercent);
            } else if (key == "icloakchancepercent") {
                g_config.cloakChancePercent = ParseInt(value, g_config.cloakChancePercent);
            } else if (key == "iscarfinsteadofcloakchancepercent") {
                g_config.scarfInsteadOfCloakChancePercent = ParseInt(value, g_config.scarfInsteadOfCloakChancePercent);
            }
        } else if (section == "debug") {
            if (key == "benablelogging") {
                g_config.enableLogging = ParseBool(value, g_config.enableLogging);
            }
        } else if (section == "timing") {
            if (key == "finteriorunequipmin") {
                g_config.interiorUnequipMin = ParseFloat(value, g_config.interiorUnequipMin);
            } else if (key == "finteriorunequipmax") {
                g_config.interiorUnequipMax = ParseFloat(value, g_config.interiorUnequipMax);
            } else if (key == "fweatherequipmin") {
                g_config.weatherEquipMin = ParseFloat(value, g_config.weatherEquipMin);
            } else if (key == "fweatherequipmax") {
                g_config.weatherEquipMax = ParseFloat(value, g_config.weatherEquipMax);
            } else if (key == "fclearunequipmin") {
                g_config.clearUnequipMin = ParseFloat(value, g_config.clearUnequipMin);
            } else if (key == "fclearunequipmax") {
                g_config.clearUnequipMax = ParseFloat(value, g_config.clearUnequipMax);
            }
        } else if (section == "combat") {
            if (key == "bdisablecloaksincombat") {
                g_config.disableCloaksInCombat = ParseBool(value, g_config.disableCloaksInCombat);
            }
        }
    }

    ApplyConfigSanity();
}