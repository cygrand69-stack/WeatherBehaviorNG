#include "Config.h"
#include "GearSystem.h"
#include "Logger.h"
#include "SKSE/SKSE.h"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    LoadConfig();
    WBNG::Log::Init();
    WBNG_LOG_INFO("SKSE plugin load");

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        WBNG_LOG_ERROR("Failed to get messaging interface");
        return false;
    }

    messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (!msg) {
            return;
        }

        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                WBNG_LOG_INFO("Received kDataLoaded");
                WBNG::OnDataLoaded();
                break;

            case SKSE::MessagingInterface::kPreLoadGame:
                WBNG_LOG_INFO("Received kPreLoadGame");
                WBNG::OnPreLoadGame();
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                WBNG_LOG_INFO("Received kPostLoadGame");
                WBNG::OnPostLoadGame();
                break;

            case SKSE::MessagingInterface::kNewGame:
                WBNG_LOG_INFO("Received kNewGame");
                WBNG::OnNewGame();
                break;

            default:
                break;
        }
    });

    return true;
}