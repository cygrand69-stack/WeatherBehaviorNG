#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "RE/A/AIProcess.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
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
    };

    std::atomic_bool g_running = false;
    std::atomic_bool g_runtimeReady = false;
    std::thread g_worker;
    std::mt19937 g_rng(std::random_device{}());

    Config g_config{};
    float g_updateRadiusSquared = g_config.updateRadius * g_config.updateRadius;

    enum class WeatherKind { kNone, kRain, kSnow };

    struct GearPools {
        std::vector<RE::TESObjectARMO*> rainHoods;
        std::vector<RE::TESObjectARMO*> snowHoods;
        std::vector<RE::TESObjectARMO*> rainCloaks;
        std::vector<RE::TESObjectARMO*> snowCloaks;
    };

    struct ActorState {
        RE::TESObjectARMO* managedHood = nullptr;
        RE::TESObjectARMO* managedCloak = nullptr;

        bool addedHoodToInventory = false;
        bool addedCloakToInventory = false;

        float nextAllowedUpdate = 0.0f;
    };

    GearPools g_pools;
    std::unordered_map<RE::FormID, ActorState> g_actorStates;

    RE::TESWeather* g_lastWeather = nullptr;
    float g_lastWeatherChangeTime = 0.0f;

    float GetNowSeconds() {
        using Clock = std::chrono::steady_clock;
        static const auto startTime = Clock::now();
        return std::chrono::duration<float>(Clock::now() - startTime).count();
    }

    float RandomFloat(float minValue, float maxValue) {
        if (maxValue < minValue) {
            std::swap(minValue, maxValue);
        }

        std::uniform_real_distribution<float> dist(minValue, maxValue);
        return dist(g_rng);
    }

    std::uint32_t HashActor(RE::FormID actorID, std::uint32_t salt) {
        std::uint32_t x = actorID ^ salt;
        x ^= x >> 16;
        x *= 0x7feb352d;
        x ^= x >> 15;
        x *= 0x846ca68b;
        x ^= x >> 16;
        return x;
    }

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

        g_updateRadiusSquared = g_config.updateRadius * g_config.updateRadius;
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

    bool ArmorHasKeyword(RE::TESObjectARMO* armor, std::string_view keywordEditorID) {
        return armor && armor->HasKeywordString(keywordEditorID);
    }

    bool ActorHasKeyword(RE::Actor* actor, std::string_view keywordEditorID) {
        return actor && actor->HasKeywordString(keywordEditorID);
    }

    bool IsValidNPC(RE::Actor* actor) {
        return actor && !actor->IsPlayerRef() && !actor->IsDead() && actor->GetActorBase() &&
               actor->HasKeywordString("ActorTypeNPC");
    }

    bool IsWithinUpdateRange(RE::Actor* actor, RE::PlayerCharacter* player) {
        if (!actor || !player) {
            return false;
        }

        const auto a = actor->GetPosition();
        const auto p = player->GetPosition();

        const float dx = a.x - p.x;
        const float dy = a.y - p.y;
        const float dz = a.z - p.z;

        return (dx * dx + dy * dy + dz * dz) <= g_updateRadiusSquared;
    }

    bool IsInterior(RE::Actor* actor) {
        auto* cell = actor ? actor->GetParentCell() : nullptr;
        return cell ? cell->IsInteriorCell() : false;
    }

    bool IsInventoryLikeMenuOpen() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        return ui->IsMenuOpen("ContainerMenu") || ui->IsMenuOpen("InventoryMenu") || ui->IsMenuOpen("BarterMenu") ||
               ui->IsMenuOpen("GiftMenu");
    }

    WeatherKind GetWeatherKind() {
        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather) {
            return WeatherKind::kNone;
        }

        auto* weather = sky->currentWeather;
        auto flags = weather->data.flags;

        if (flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
            return WeatherKind::kSnow;
        }

        if (flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
            return WeatherKind::kRain;
        }

        return WeatherKind::kNone;
    }

    void UpdateWeatherTransition(float now) {
        auto* sky = RE::Sky::GetSingleton();
        auto* currentWeather = sky ? sky->currentWeather : nullptr;

        if (currentWeather != g_lastWeather) {
            g_lastWeather = currentWeather;
            g_lastWeatherChangeTime = now;
        }
    }

    bool IsWeatherStable(float now) { return (now - g_lastWeatherChangeTime) >= g_config.weatherTransitionDelay; }

    void BuildGearPools() {
        g_pools = {};

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return;
        }

        auto& armors = dataHandler->GetFormArray<RE::TESObjectARMO>();
        for (auto* armor : armors) {
            if (!armor) {
                continue;
            }

            if (ArmorHasKeyword(armor, "WBNG_Hood_Rain")) {
                g_pools.rainHoods.push_back(armor);
            }
            if (ArmorHasKeyword(armor, "WBNG_Hood_Snow")) {
                g_pools.snowHoods.push_back(armor);
            }
            if (ArmorHasKeyword(armor, "WBNG_Cloak_Rain")) {
                g_pools.rainCloaks.push_back(armor);
            }
            if (ArmorHasKeyword(armor, "WBNG_Cloak_Snow")) {
                g_pools.snowCloaks.push_back(armor);
            }
        }
    }

    RE::TESObjectARMO* PickStableItem(RE::FormID actorID, const std::vector<RE::TESObjectARMO*>& pool,
                                      std::uint32_t salt) {
        if (pool.empty()) {
            return nullptr;
        }

        const auto index = HashActor(actorID, salt) % static_cast<std::uint32_t>(pool.size());
        return pool[index];
    }

    RE::TESObjectARMO* PickDesiredHood(RE::FormID actorID, WeatherKind kind) {
        switch (kind) {
            case WeatherKind::kRain:
                return PickStableItem(actorID, g_pools.rainHoods, 0xA1B2C3D4);
            case WeatherKind::kSnow:
                return PickStableItem(actorID, g_pools.snowHoods, 0xC4D3B2A1);
            default:
                return nullptr;
        }
    }

    RE::TESObjectARMO* PickDesiredCloak(RE::FormID actorID, WeatherKind kind) {
        switch (kind) {
            case WeatherKind::kRain:
                return PickStableItem(actorID, g_pools.rainCloaks, 0x11223344);
            case WeatherKind::kSnow:
                return PickStableItem(actorID, g_pools.snowCloaks, 0x44332211);
            default:
                return nullptr;
        }
    }

    bool HasItemInInventory(RE::Actor* actor, RE::TESBoundObject* item) {
        if (!actor || !item) {
            return false;
        }

        auto inventory = actor->GetInventory();
        return inventory.find(item) != inventory.end();
    }

    bool IsWearingItem(RE::Actor* actor, RE::TESObjectARMO* item) {
        if (!actor || !item) {
            return false;
        }

        auto inventory = actor->GetInventory();

        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            RE::TESBoundObject* obj = it->first;
            auto& mapped = it->second;

            if (obj != item) {
                continue;
            }

            auto* entryData = mapped.second.get();
            if (entryData && entryData->IsWorn()) {
                return true;
            }
        }

        return false;
    }

    bool IsManagedCloak(RE::TESObjectARMO* armor) {
        return armor && (ArmorHasKeyword(armor, "WBNG_Cloak_Rain") || ArmorHasKeyword(armor, "WBNG_Cloak_Snow"));
    }

    RE::TESObjectARMO* GetEquippedManagedCloak(RE::Actor* actor) {
        if (!actor) {
            return nullptr;
        }

        auto inventory = actor->GetInventory();

        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            auto* obj = it->first;
            auto& mapped = it->second;

            auto* armor = obj ? obj->As<RE::TESObjectARMO>() : nullptr;
            if (!armor) {
                continue;
            }

            if (!IsManagedCloak(armor)) {
                continue;
            }

            auto* entryData = mapped.second.get();
            if (entryData && entryData->IsWorn()) {
                return armor;
            }
        }

        return nullptr;
    }

    bool HasOtherManagedCloakEquipped(RE::Actor* actor, RE::TESObjectARMO* allowedCloak) {
        auto* wornManagedCloak = GetEquippedManagedCloak(actor);
        return wornManagedCloak && wornManagedCloak != allowedCloak;
    }

    bool HasBlockingHeadgear(RE::Actor* actor, RE::TESObjectARMO* allowedHood) {
        if (!actor) {
            return false;
        }

        constexpr RE::BGSBipedObjectForm::BipedObjectSlot blockedSlots[] = {
            RE::BGSBipedObjectForm::BipedObjectSlot::kHead, RE::BGSBipedObjectForm::BipedObjectSlot::kHair,
            RE::BGSBipedObjectForm::BipedObjectSlot::kLongHair, RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet,
            RE::BGSBipedObjectForm::BipedObjectSlot::kEars};

        for (auto slot : blockedSlots) {
            auto* worn = actor->GetWornArmor(slot);
            if (!worn) {
                continue;
            }

            if (worn == allowedHood) {
                continue;
            }

            return true;
        }

        return false;
    }

    bool IsSlot46OccupiedByOther(RE::Actor* actor, RE::TESObjectARMO* allowedCloak) {
        if (!actor) {
            return false;
        }

        auto* worn = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kModChestPrimary);
        return worn && worn != allowedCloak;
    }

    void AddItemIfMissing(RE::Actor* actor, RE::TESObjectARMO* item, bool& addedByPlugin) {
        if (!actor || !item) {
            return;
        }

        if (HasItemInInventory(actor, item)) {
            addedByPlugin = false;
            return;
        }

        actor->AddObjectToContainer(item, nullptr, 1, nullptr);
        addedByPlugin = true;
    }

    void EquipItem(RE::Actor* actor, RE::TESObjectARMO* item) {
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        if (equipMgr && actor && item) {
            equipMgr->EquipObject(actor, item);
        }
    }

    void UnequipItem(RE::Actor* actor, RE::TESObjectARMO* item) {
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        if (equipMgr && actor && item) {
            equipMgr->UnequipObject(actor, item);
        }
    }

    void RemoveManagedHood(RE::Actor* actor, ActorState& state) {
        if (!actor || !state.managedHood) {
            return;
        }

        if (IsWearingItem(actor, state.managedHood)) {
            UnequipItem(actor, state.managedHood);
        }

        if (state.addedHoodToInventory) {
            actor->RemoveItem(state.managedHood, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr, nullptr,
                              nullptr);
        }

        state.managedHood = nullptr;
        state.addedHoodToInventory = false;
    }

    void RemoveManagedCloak(RE::Actor* actor, ActorState& state) {
        if (!actor || !state.managedCloak) {
            return;
        }

        if (IsWearingItem(actor, state.managedCloak)) {
            UnequipItem(actor, state.managedCloak);
        }

        if (state.addedCloakToInventory) {
            actor->RemoveItem(state.managedCloak, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr, nullptr,
                              nullptr);
        }

        state.managedCloak = nullptr;
        state.addedCloakToInventory = false;
    }

    void EnsureManagedHood(RE::Actor* actor, RE::TESObjectARMO* desiredHood, ActorState& state) {
        if (!actor) {
            return;
        }

        if (!desiredHood) {
            RemoveManagedHood(actor, state);
            return;
        }

        if (state.managedHood && state.managedHood != desiredHood) {
            RemoveManagedHood(actor, state);
        }

        if (HasBlockingHeadgear(actor, desiredHood)) {
            RemoveManagedHood(actor, state);
            return;
        }

        if (!state.managedHood) {
            AddItemIfMissing(actor, desiredHood, state.addedHoodToInventory);
            state.managedHood = desiredHood;
        }

        if (!IsWearingItem(actor, desiredHood)) {
            EquipItem(actor, desiredHood);
        }
    }

    void EnsureManagedCloak(RE::Actor* actor, RE::TESObjectARMO* desiredCloak, ActorState& state) {
        if (!actor) {
            return;
        }

        if (!desiredCloak) {
            RemoveManagedCloak(actor, state);
            return;
        }

        if (state.managedCloak && state.managedCloak != desiredCloak) {
            RemoveManagedCloak(actor, state);
        }

        if (HasOtherManagedCloakEquipped(actor, desiredCloak)) {
            return;
        }

        if (IsSlot46OccupiedByOther(actor, desiredCloak)) {
            RemoveManagedCloak(actor, state);
            return;
        }

        if (!state.managedCloak) {
            AddItemIfMissing(actor, desiredCloak, state.addedCloakToInventory);
            state.managedCloak = desiredCloak;
        }

        if (!IsWearingItem(actor, desiredCloak)) {
            EquipItem(actor, desiredCloak);
        }
    }

    void ProcessActor(RE::Actor* actor, float now, WeatherKind weatherKind) {
        if (!IsValidNPC(actor)) {
            return;
        }

        if (ActorHasKeyword(actor, "WBNG_ExcludedNPC")) {
            auto& state = g_actorStates[actor->GetFormID()];
            RemoveManagedHood(actor, state);
            RemoveManagedCloak(actor, state);
            return;
        }

        auto& state = g_actorStates[actor->GetFormID()];
        if (now < state.nextAllowedUpdate) {
            return;
        }

        const bool interior = IsInterior(actor);
        const bool inCombat = actor->IsInCombat();

        const bool allowRain = !ActorHasKeyword(actor, "WBNG_NoRainGear");
        const bool allowSnow = !ActorHasKeyword(actor, "WBNG_NoSnowGear");
        const bool allowHood = !ActorHasKeyword(actor, "WBNG_NoHood");
        const bool allowCloak = !ActorHasKeyword(actor, "WBNG_NoCloak");

        const bool weatherAllowed =
            (weatherKind == WeatherKind::kRain && allowRain) || (weatherKind == WeatherKind::kSnow && allowSnow);

        if (interior) {
            RemoveManagedHood(actor, state);
            RemoveManagedCloak(actor, state);
            state.nextAllowedUpdate = now + RandomFloat(g_config.interiorUnequipMin, g_config.interiorUnequipMax);
            return;
        }

        if (!weatherAllowed) {
            RemoveManagedHood(actor, state);
            RemoveManagedCloak(actor, state);
            state.nextAllowedUpdate = now + RandomFloat(g_config.clearUnequipMin, g_config.clearUnequipMax);
            return;
        }

        auto* desiredHood = allowHood ? PickDesiredHood(actor->GetFormID(), weatherKind) : nullptr;
        auto* desiredCloak = allowCloak ? PickDesiredCloak(actor->GetFormID(), weatherKind) : nullptr;

        if (inCombat && g_config.disableCloaksInCombat) {
            EnsureManagedHood(actor, desiredHood, state);
            RemoveManagedCloak(actor, state);
            state.nextAllowedUpdate = now + RandomFloat(g_config.clearUnequipMin, g_config.clearUnequipMax);
            return;
        }

        EnsureManagedHood(actor, desiredHood, state);
        EnsureManagedCloak(actor, desiredCloak, state);
        state.nextAllowedUpdate = now + RandomFloat(g_config.weatherEquipMin, g_config.weatherEquipMax);
    }

    void UpdateLoadedActors() {
        if (!g_runtimeReady) {
            return;
        }

        if (IsInventoryLikeMenuOpen()) {
            return;
        }

        auto* lists = RE::ProcessLists::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!lists || !player) {
            return;
        }

        const float now = GetNowSeconds();

        UpdateWeatherTransition(now);
        if (!IsWeatherStable(now)) {
            return;
        }

        const auto weatherKind = GetWeatherKind();

        for (auto& handle : lists->highActorHandles) {
            auto actor = handle.get().get();
            if (!actor) {
                continue;
            }

            if (!IsWithinUpdateRange(actor, player)) {
                continue;
            }

            ProcessActor(actor, now, weatherKind);
        }
    }

    void StartTicker() {
        if (g_running) {
            return;
        }

        g_running = true;
        g_worker = std::thread([] {
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(g_config.tickerIntervalMs));

                auto* task = SKSE::GetTaskInterface();
                if (task) {
                    task->AddTask(UpdateLoadedActors);
                }
            }
        });
    }

    [[maybe_unused]] void StopTicker() {
        g_running = false;

        if (g_worker.joinable()) {
            g_worker.join();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        return false;
    }

    messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (!msg) {
            return;
        }

        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                LoadConfig();
                BuildGearPools();
                g_runtimeReady = false;
                break;

            case SKSE::MessagingInterface::kPreLoadGame:
                g_runtimeReady = false;
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                g_runtimeReady = true;
                StartTicker();
                UpdateLoadedActors();
                break;

            case SKSE::MessagingInterface::kNewGame:
                g_runtimeReady = true;
                StartTicker();
                UpdateLoadedActors();
                break;

            default:
                break;
        }
    });

    return true;
}