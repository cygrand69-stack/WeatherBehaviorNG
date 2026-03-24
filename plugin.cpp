#include <atomic>
#include <chrono>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace {
    std::atomic_bool g_running = false;
    std::thread g_worker;

    std::mt19937 g_rng(std::random_device{}());

    std::unordered_map<RE::FormID, float> g_pendingEquipTime;
    std::unordered_map<RE::FormID, float> g_pendingUnequipTime;
    std::unordered_map<RE::FormID, float> g_cooldownUntil;

    constexpr float kUpdateRadius = 6000.0f;
    constexpr float kUpdateRadiusSquared = kUpdateRadius * kUpdateRadius;
    constexpr float kWeatherTransitionDelay = 3.0f;

    RE::TESWeather* g_lastWeather = nullptr;
    float g_lastWeatherChangeTime = 0.0f;

    enum class WeatherKind { kNone, kRain, kSnow };

    float RandomFloat(float minValue, float maxValue) {
        std::uniform_real_distribution<float> dist(minValue, maxValue);
        return dist(g_rng);
    }

    float GetNowSeconds() {
        using Clock = std::chrono::steady_clock;
        static const auto startTime = Clock::now();
        return std::chrono::duration<float>(Clock::now() - startTime).count();
    }

    WeatherKind GetWeatherKind() {
        auto sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather) {
            return WeatherKind::kNone;
        }

        auto* weather = sky->currentWeather;

        if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
            return WeatherKind::kSnow;
        }

        if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
            return WeatherKind::kRain;
        }

        return WeatherKind::kNone;
    }

    bool IsBadWeather(WeatherKind kind) { return kind == WeatherKind::kRain || kind == WeatherKind::kSnow; }

    void UpdateWeatherTransition(float now) {
        auto sky = RE::Sky::GetSingleton();
        auto currentWeather = sky ? sky->currentWeather : nullptr;

        if (currentWeather != g_lastWeather) {
            g_lastWeather = currentWeather;
            g_lastWeatherChangeTime = now;
        }
    }

    bool IsWeatherStable(float now) { return (now - g_lastWeatherChangeTime) >= kWeatherTransitionDelay; }

    bool IsValidNPC(RE::Actor* actor) {
        return actor && !actor->IsPlayerRef() && !actor->IsDead() && actor->GetActorBase();
    }

    bool ShouldSkipState(RE::Actor* actor) { return actor && actor->IsInCombat(); }

    bool IsOutside(RE::Actor* actor) {
        auto cell = actor ? actor->GetParentCell() : nullptr;
        return cell && cell->IsExteriorCell();
    }

    bool IsWithinUpdateRange(RE::Actor* actor, RE::PlayerCharacter* player) {
        auto a = actor ? actor->GetPosition() : RE::NiPoint3{};
        auto p = player ? player->GetPosition() : RE::NiPoint3{};

        float dx = a.x - p.x;
        float dy = a.y - p.y;
        float dz = a.z - p.z;

        return (dx * dx + dy * dy + dz * dz) <= kUpdateRadiusSquared;
    }

    RE::TESObjectARMO* FindArmorWithKeywordString(RE::Actor* actor, std::string_view keywordEditorID) {
        if (!actor || keywordEditorID.empty()) {
            return nullptr;
        }

        auto inv = actor->GetInventory();
        for (auto& [item, entry] : inv) {
            if (!item) {
                continue;
            }

            auto armor = item->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            if (armor->HasKeywordString(keywordEditorID)) {
                return armor;
            }
        }

        return nullptr;
    }

    bool IsManagedHood(RE::TESObjectARMO* armor) {
        if (!armor) {
            return false;
        }

        return armor->HasKeywordString("WBNG_Hood_Rain") || armor->HasKeywordString("WBNG_Hood_Snow");
    }

    RE::TESObjectARMO* GetEquippedManagedHood(RE::Actor* actor) {
        if (!actor) {
            return nullptr;
        }

        auto inv = actor->GetInventory();
        for (auto& [item, entry] : inv) {
            if (!item) {
                continue;
            }

            auto armor = item->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            auto* invData = entry.second.get();
            if (!invData || !invData->IsWorn()) {
                continue;
            }

            if (IsManagedHood(armor)) {
                return armor;
            }
        }

        return nullptr;
    }

    bool HasBlockingHelmet(RE::Actor* actor, RE::TESBoundObject* activeHood) {
        if (!actor) {
            return false;
        }

        constexpr RE::BGSBipedObjectForm::BipedObjectSlot kBlockedSlots[] = {
            RE::BGSBipedObjectForm::BipedObjectSlot::kHead, RE::BGSBipedObjectForm::BipedObjectSlot::kHair,
            RE::BGSBipedObjectForm::BipedObjectSlot::kLongHair, RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet,
            RE::BGSBipedObjectForm::BipedObjectSlot::kEars};

        for (auto slot : kBlockedSlots) {
            auto worn = actor->GetWornArmor(slot);
            if (!worn) {
                continue;
            }

            if (worn == activeHood) {
                continue;
            }

            return true;
        }

        return false;
    }

    bool IsWearingManagedHood(RE::Actor* actor, RE::TESBoundObject* item) {
        return GetEquippedManagedHood(actor) == item;
    }

    RE::TESBoundObject* GetSlot46Item(RE::Actor* actor) {
        auto worn = actor ? actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kModChestPrimary) : nullptr;
        return worn ? worn->As<RE::TESBoundObject>() : nullptr;
    }

    bool IsWearingItemInSlot46(RE::Actor* actor, RE::TESBoundObject* item) { return GetSlot46Item(actor) == item; }

    bool IsWearingOtherSlot46Item(RE::Actor* actor, RE::TESBoundObject* item) {
        auto worn = GetSlot46Item(actor);
        return worn && worn != item;
    }

    void EquipItem(RE::Actor* actor, RE::TESBoundObject* item) {
        auto mgr = RE::ActorEquipManager::GetSingleton();
        if (mgr && actor && item) {
            mgr->EquipObject(actor, item);
        }
    }

    bool UnequipItemNow(RE::Actor* actor, RE::TESBoundObject* item) {
        auto mgr = RE::ActorEquipManager::GetSingleton();
        return mgr && actor && item && mgr->UnequipObject(actor, item);
    }

    bool IsOnCooldown(RE::FormID id, float now) {
        auto it = g_cooldownUntil.find(id);
        return it != g_cooldownUntil.end() && now < it->second;
    }

    void StartCooldown(RE::FormID id, float now) { g_cooldownUntil[id] = now + RandomFloat(3.0f, 6.0f); }

    void QueueEquip(RE::Actor* actor, RE::FormID id, float now) {
        if (g_pendingEquipTime.contains(id)) {
            return;
        }

        bool isFemale = actor && actor->GetActorBase() && actor->GetActorBase()->GetSex() == RE::SEX::kFemale;

        float minDelay = isFemale ? 0.5f : 1.5f;
        float maxDelay = isFemale ? 2.0f : 4.0f;

        g_pendingEquipTime[id] = now + RandomFloat(minDelay, maxDelay);
    }

    void QueueUnequipIndoor(RE::FormID id, float now) {
        if (!g_pendingUnequipTime.contains(id)) {
            g_pendingUnequipTime[id] = now + RandomFloat(0.2f, 1.0f);
        }
    }

    void QueueUnequipPleasant(RE::FormID id, float now) {
        if (!g_pendingUnequipTime.contains(id)) {
            g_pendingUnequipTime[id] = now + RandomFloat(1.0f, 3.0f);
        }
    }

    template <class TMap>
    bool IsReady(const TMap& map, RE::FormID id, float now) {
        auto it = map.find(id);
        return it != map.end() && now >= it->second;
    }

    void UpdateLoadedActors() {
        auto lists = RE::ProcessLists::GetSingleton();
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!lists || !player) {
            return;
        }

        float now = GetNowSeconds();

        UpdateWeatherTransition(now);
        if (!IsWeatherStable(now)) {
            return;
        }

        WeatherKind weatherKind = GetWeatherKind();
        bool badWeather = IsBadWeather(weatherKind);

        for (auto& handle : lists->highActorHandles) {
            auto actor = handle.get().get();
            if (!IsValidNPC(actor)) {
                continue;
            }

            if (!IsWithinUpdateRange(actor, player)) {
                continue;
            }

            auto rainHood = FindArmorWithKeywordString(actor, "WBNG_Hood_Rain");
            auto snowHood = FindArmorWithKeywordString(actor, "WBNG_Hood_Snow");
            auto rainCloak = FindArmorWithKeywordString(actor, "WBNG_Cloak_Rain");
            auto snowCloak = FindArmorWithKeywordString(actor, "WBNG_Cloak_Snow");

            RE::TESObjectARMO* activeHood = nullptr;
            RE::TESObjectARMO* activeCloak = nullptr;

            if (weatherKind == WeatherKind::kRain) {
                activeHood = rainHood;
                activeCloak = rainCloak;
            } else if (weatherKind == WeatherKind::kSnow) {
                activeHood = snowHood;
                activeCloak = snowCloak;
            }

            if (!rainHood && !snowHood && !rainCloak && !snowCloak) {
                continue;
            }

            auto id = actor->GetFormID();
            bool outside = IsOutside(actor);

            bool wearingRainHood = rainHood && IsWearingManagedHood(actor, rainHood);
            bool wearingSnowHood = snowHood && IsWearingManagedHood(actor, snowHood);
            bool wearingActiveHood = activeHood && IsWearingManagedHood(actor, activeHood);
            bool hasBlockingHelmet = HasBlockingHelmet(actor, activeHood);

            bool wearingRainCloak = rainCloak && IsWearingItemInSlot46(actor, rainCloak);
            bool wearingSnowCloak = snowCloak && IsWearingItemInSlot46(actor, snowCloak);
            bool wearingActiveCloak = activeCloak && IsWearingItemInSlot46(actor, activeCloak);
            bool wearingOtherSlot46 = activeCloak && IsWearingOtherSlot46Item(actor, activeCloak);

            if (ShouldSkipState(actor)) {
                g_pendingEquipTime.erase(id);

                QueueUnequipPleasant(id, now);

                if (IsReady(g_pendingUnequipTime, id, now)) {
                    bool changed = false;

                    if (rainCloak) {
                        changed |= UnequipItemNow(actor, rainCloak);
                    }
                    if (snowCloak) {
                        changed |= UnequipItemNow(actor, snowCloak);
                    }

                    if (changed) {
                        actor->EvaluatePackage(false, true);
                        StartCooldown(id, now);
                    }

                    g_pendingUnequipTime.erase(id);
                }

                continue;
            }

            if (IsOnCooldown(id, now)) {
                continue;
            }

            if (outside && badWeather) {
                g_pendingUnequipTime.erase(id);

                bool removedWrongSeasonItem = false;

                if (weatherKind == WeatherKind::kRain) {
                    if (wearingSnowHood && snowHood) {
                        removedWrongSeasonItem |= UnequipItemNow(actor, snowHood);
                    }
                    if (wearingSnowCloak && snowCloak) {
                        removedWrongSeasonItem |= UnequipItemNow(actor, snowCloak);
                    }
                } else if (weatherKind == WeatherKind::kSnow) {
                    if (wearingRainHood && rainHood) {
                        removedWrongSeasonItem |= UnequipItemNow(actor, rainHood);
                    }
                    if (wearingRainCloak && rainCloak) {
                        removedWrongSeasonItem |= UnequipItemNow(actor, rainCloak);
                    }
                }

                if (removedWrongSeasonItem) {
                    actor->EvaluatePackage(false, true);
                    StartCooldown(id, now);
                    g_pendingEquipTime.erase(id);
                    continue;
                }

                bool canEquipHood = activeHood && !wearingActiveHood && !hasBlockingHelmet;
                bool canEquipCloak = activeCloak && !wearingActiveCloak && !wearingOtherSlot46;

                if (canEquipHood || canEquipCloak) {
                    QueueEquip(actor, id, now);

                    if (IsReady(g_pendingEquipTime, id, now)) {
                        if (canEquipHood) {
                            EquipItem(actor, activeHood);
                        }
                        if (canEquipCloak) {
                            EquipItem(actor, activeCloak);
                        }

                        g_pendingEquipTime.erase(id);
                        StartCooldown(id, now);
                    }
                } else {
                    g_pendingEquipTime.erase(id);
                }
            } else {
                g_pendingEquipTime.erase(id);

                if (!outside) {
                    QueueUnequipIndoor(id, now);
                } else {
                    QueueUnequipPleasant(id, now);
                }

                if (IsReady(g_pendingUnequipTime, id, now)) {
                    bool changed = false;

                    if (rainHood) {
                        changed |= UnequipItemNow(actor, rainHood);
                    }
                    if (snowHood) {
                        changed |= UnequipItemNow(actor, snowHood);
                    }
                    if (rainCloak) {
                        changed |= UnequipItemNow(actor, rainCloak);
                    }
                    if (snowCloak) {
                        changed |= UnequipItemNow(actor, snowCloak);
                    }

                    if (changed) {
                        actor->EvaluatePackage(false, true);
                        StartCooldown(id, now);
                    }

                    g_pendingUnequipTime.erase(id);
                }
            }
        }
    }

    void StartWeatherTicker() {
        if (g_running) {
            return;
        }

        g_running = true;

        g_worker = std::thread([] {
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                auto task = SKSE::GetTaskInterface();
                if (task) {
                    task->AddTask(UpdateLoadedActors);
                }
            }
        });
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            UpdateLoadedActors();
            StartWeatherTicker();
        }

        if (msg->type == SKSE::MessagingInterface::kPostLoadGame) {
            UpdateLoadedActors();
        }
    });

    return true;
}