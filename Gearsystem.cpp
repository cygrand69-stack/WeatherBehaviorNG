#include "GearSystem.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Config.h"
#include "RE/A/AIProcess.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    std::atomic_bool g_running = false;
    std::atomic_bool g_runtimeReady = false;
    std::thread g_worker;
    std::mt19937 g_rng(std::random_device{}());

    enum class WeatherKind { kNone, kRain, kSnow };

    struct GearPools {
        std::vector<RE::TESObjectARMO*> rainHoods;
        std::vector<RE::TESObjectARMO*> snowHoods;
        std::vector<RE::TESObjectARMO*> rainCloaks;
        std::vector<RE::TESObjectARMO*> snowCloaks;
        std::vector<RE::TESObjectARMO*> rainScarves;
        std::vector<RE::TESObjectARMO*> snowScarves;
    };

    struct ActorState {
        RE::TESObjectARMO* managedHood = nullptr;
        RE::TESObjectARMO* managedCloak = nullptr;
        RE::TESObjectARMO* managedScarf = nullptr;

        bool addedHoodToInventory = false;
        bool addedCloakToInventory = false;
        bool addedScarfToInventory = false;

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

    bool PassesStableChance(RE::FormID actorID, std::uint32_t salt, int percentChance) {
        if (percentChance <= 0) {
            return false;
        }
        if (percentChance >= 100) {
            return true;
        }

        const auto roll = HashActor(actorID, salt) % 100;
        return roll < static_cast<std::uint32_t>(percentChance);
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

    void ResetRuntimeState() {
        g_actorStates.clear();
        g_lastWeather = nullptr;
        g_lastWeatherChangeTime = 0.0f;
    }

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
            if (ArmorHasKeyword(armor, "WBNG_Scarf_Rain")) {
                g_pools.rainScarves.push_back(armor);
            }
            if (ArmorHasKeyword(armor, "WBNG_Scarf_Snow")) {
                g_pools.snowScarves.push_back(armor);
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

    RE::TESObjectARMO* PickDesiredScarf(RE::FormID actorID, WeatherKind kind) {
        switch (kind) {
            case WeatherKind::kRain:
                return PickStableItem(actorID, g_pools.rainScarves, 0x55667788);
            case WeatherKind::kSnow:
                return PickStableItem(actorID, g_pools.snowScarves, 0x88776655);
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

    bool IsManagedScarf(RE::TESObjectARMO* armor) {
        return armor && (ArmorHasKeyword(armor, "WBNG_Scarf_Rain") || ArmorHasKeyword(armor, "WBNG_Scarf_Snow"));
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

    RE::TESObjectARMO* GetEquippedManagedScarf(RE::Actor* actor) {
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

            if (!IsManagedScarf(armor)) {
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

    bool HasOtherManagedScarfEquipped(RE::Actor* actor, RE::TESObjectARMO* allowedScarf) {
        auto* wornManagedScarf = GetEquippedManagedScarf(actor);
        return wornManagedScarf && wornManagedScarf != allowedScarf;
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

    bool HasConflictingWornArmor(RE::Actor* actor, RE::TESObjectARMO* candidate) {
        if (!actor || !candidate) {
            return false;
        }

        const auto candidateMask = static_cast<std::uint32_t>(candidate->GetSlotMask());
        if (candidateMask == 0) {
            return false;
        }

        auto inventory = actor->GetInventory();

        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            auto* obj = it->first;
            auto& mapped = it->second;

            auto* wornArmor = obj ? obj->As<RE::TESObjectARMO>() : nullptr;
            if (!wornArmor) {
                continue;
            }

            if (wornArmor == candidate) {
                continue;
            }

            auto* entryData = mapped.second.get();
            if (!entryData || !entryData->IsWorn()) {
                continue;
            }

            const auto wornMask = static_cast<std::uint32_t>(wornArmor->GetSlotMask());
            if ((candidateMask & wornMask) != 0) {
                return true;
            }
        }

        return false;
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

    void RemoveManagedScarf(RE::Actor* actor, ActorState& state) {
        if (!actor || !state.managedScarf) {
            return;
        }

        if (IsWearingItem(actor, state.managedScarf)) {
            UnequipItem(actor, state.managedScarf);
        }

        if (state.addedScarfToInventory) {
            actor->RemoveItem(state.managedScarf, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr, nullptr,
                              nullptr);
        }

        state.managedScarf = nullptr;
        state.addedScarfToInventory = false;
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

        if (HasConflictingWornArmor(actor, desiredCloak)) {
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

    void EnsureManagedScarf(RE::Actor* actor, RE::TESObjectARMO* desiredScarf, ActorState& state) {
        if (!actor) {
            return;
        }

        if (!desiredScarf) {
            RemoveManagedScarf(actor, state);
            return;
        }

        if (state.managedScarf && state.managedScarf != desiredScarf) {
            RemoveManagedScarf(actor, state);
        }

        if (HasOtherManagedScarfEquipped(actor, desiredScarf)) {
            return;
        }

        if (HasConflictingWornArmor(actor, desiredScarf)) {
            RemoveManagedScarf(actor, state);
            return;
        }

        if (!state.managedScarf) {
            AddItemIfMissing(actor, desiredScarf, state.addedScarfToInventory);
            state.managedScarf = desiredScarf;
        }

        if (!IsWearingItem(actor, desiredScarf)) {
            EquipItem(actor, desiredScarf);
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
            RemoveManagedScarf(actor, state);
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

        const bool allowHood = !ActorHasKeyword(actor, "WBNG_NoHood") &&
                               PassesStableChance(actor->GetFormID(), 0xB00D1234, g_config.hoodChancePercent);

        const bool allowCloak = !ActorHasKeyword(actor, "WBNG_NoCloak") &&
                                PassesStableChance(actor->GetFormID(), 0xC10A1234, g_config.cloakChancePercent);

        const bool allowScarf = !ActorHasKeyword(actor, "WBNG_NoScarf") &&
                                PassesStableChance(actor->GetFormID(), 0x5CAF1234, g_config.scarfChancePercent);

        const bool weatherAllowed =
            (weatherKind == WeatherKind::kRain && allowRain) || (weatherKind == WeatherKind::kSnow && allowSnow);

        if (interior) {
            RemoveManagedHood(actor, state);
            RemoveManagedCloak(actor, state);
            RemoveManagedScarf(actor, state);
            state.nextAllowedUpdate = now + RandomFloat(g_config.interiorUnequipMin, g_config.interiorUnequipMax);
            return;
        }

        if (!weatherAllowed) {
            RemoveManagedHood(actor, state);
            RemoveManagedCloak(actor, state);
            RemoveManagedScarf(actor, state);
            state.nextAllowedUpdate = now + RandomFloat(g_config.clearUnequipMin, g_config.clearUnequipMax);
            return;
        }

        auto* desiredHood = allowHood ? PickDesiredHood(actor->GetFormID(), weatherKind) : nullptr;
        auto* desiredCloak = allowCloak ? PickDesiredCloak(actor->GetFormID(), weatherKind) : nullptr;
        auto* desiredScarf = allowScarf ? PickDesiredScarf(actor->GetFormID(), weatherKind) : nullptr;
        const bool preferScarfOverCloak =
            desiredScarf && desiredCloak &&
            PassesStableChance(actor->GetFormID(), 0x51A2F00D, g_config.scarfInsteadOfCloakChancePercent);

        if (preferScarfOverCloak) {
            desiredCloak = nullptr;
        } else if (desiredCloak) {
            desiredScarf = nullptr;
        }

        if (inCombat && g_config.disableCloaksInCombat) {
            EnsureManagedHood(actor, desiredHood, state);
            EnsureManagedScarf(actor, desiredScarf, state);
            RemoveManagedCloak(actor, state);
            state.nextAllowedUpdate = now + RandomFloat(g_config.clearUnequipMin, g_config.clearUnequipMax);
            return;
        }

        EnsureManagedHood(actor, desiredHood, state);
        EnsureManagedCloak(actor, desiredCloak, state);
        EnsureManagedScarf(actor, desiredScarf, state);
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

namespace WBNG {
    void OnDataLoaded() {
        LoadConfig();
        ResetRuntimeState();
        BuildGearPools();
        g_runtimeReady = false;
    }

    void OnPreLoadGame() {
        g_runtimeReady = false;
        ResetRuntimeState();
    }

    void OnPostLoadGame() {
        ResetRuntimeState();
        g_runtimeReady = true;
        StartTicker();
        UpdateLoadedActors();
    }

    void OnNewGame() {
        ResetRuntimeState();
        g_runtimeReady = true;
        StartTicker();
        UpdateLoadedActors();
    }
}