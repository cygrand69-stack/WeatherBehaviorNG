#pragma once

struct Config {
    float updateRadius = 6000.0f;
    float weatherTransitionDelay = 3.0f;
    int tickerIntervalMs = 500;

    float interiorUnequipMin = 0.5f;
    float interiorUnequipMax = 1.5f;

    float weatherEquipMin = 2.0f;
    float weatherEquipMax = 4.0f;

    float clearUnequipMin = 1.0f;
    float clearUnequipMax = 3.0f;

    bool disableCloaksInCombat = true;
    int hoodChancePercent = 100;
    int cloakChancePercent = 100;
    int scarfChancePercent = 35;
    int scarfInsteadOfCloakChancePercent = 25;

    bool enableLogging = false;
};

extern Config g_config;
extern float g_updateRadiusSquared;

void LoadConfig();