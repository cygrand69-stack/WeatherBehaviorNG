#include "GearSystem.h"
#include "SKSE/SKSE.h"

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
                WBNG::OnDataLoaded();
                break;

            case SKSE::MessagingInterface::kPreLoadGame:
                WBNG::OnPreLoadGame();
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                WBNG::OnPostLoadGame();
                break;

            case SKSE::MessagingInterface::kNewGame:
                WBNG::OnNewGame();
                break;

            default:
                break;
        }
    });

    return true;
}