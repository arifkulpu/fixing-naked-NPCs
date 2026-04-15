#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include <spdlog/sinks/basic_file_sink.h>

using namespace std::literals;

namespace
{
    struct Settings
    {
        std::uint32_t scanIntervalMs{ 5000 };
        bool includeFollowers{ true };
        bool reequipSleepOutfit{ false };
        bool skipActorsInCombat{ true };
        bool forceInventoryReset{ true };
    };

    Settings g_settings{};
    std::atomic_bool g_started{ false };
    std::atomic<std::uint64_t> g_lastScanTick{ 0 };
    std::mutex g_scanLock;

    [[nodiscard]] std::uint64_t NowMs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void LoadSettings()
    {
        constexpr auto path = "Data\\SKSE\\Plugins\\NPCOutfitFixer.ini"sv;

        std::ifstream file(path.data());
        if (!file.is_open()) {
            spdlog::warn("Settings file not found, using defaults: {}", path);
            return;
        }

        auto trim = [](std::string value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return std::string{};
            }

            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        };

        auto parseBool = [](const std::string& value, bool fallback) {
            if (_stricmp(value.c_str(), "true") == 0 || value == "1") {
                return true;
            }
            if (_stricmp(value.c_str(), "false") == 0 || value == "0") {
                return false;
            }
            return fallback;
        };

        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#'
                || line[0] == '[') {
                continue;
            }

            const auto equals = line.find('=');
            if (equals == std::string::npos) {
                continue;
            }

            const auto key = trim(line.substr(0, equals));
            const auto value = trim(line.substr(equals + 1));

            if (_stricmp(key.c_str(), "ScanIntervalMs") == 0) {
                g_settings.scanIntervalMs = static_cast<std::uint32_t>(std::stoul(value));
            } else if (_stricmp(key.c_str(), "IncludeFollowers") == 0) {
                g_settings.includeFollowers = parseBool(value, g_settings.includeFollowers);
            } else if (_stricmp(key.c_str(), "ReequipSleepOutfit") == 0) {
                g_settings.reequipSleepOutfit = parseBool(value, g_settings.reequipSleepOutfit);
            } else if (_stricmp(key.c_str(), "SkipActorsInCombat") == 0) {
                g_settings.skipActorsInCombat = parseBool(value, g_settings.skipActorsInCombat);
            } else if (_stricmp(key.c_str(), "ForceInventoryReset") == 0) {
                g_settings.forceInventoryReset = parseBool(value, g_settings.forceInventoryReset);
            }
        }
    }

    [[nodiscard]] bool HasVisibleBodyArmor(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }

        const auto worn = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
        return worn != nullptr;
    }

    [[nodiscard]] bool IsActorSleeping(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }

        const auto actorState = actor->AsActorState();
        if (!actorState) {
            return false;
        }

        return actorState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
    }

    [[nodiscard]] RE::BGSOutfit* PickOutfit(RE::Actor* actor)
    {
        if (!actor) {
            return nullptr;
        }

        const auto actorBase = actor->GetActorBase();
        if (!actorBase) {
            return nullptr;
        }

        RE::BGSOutfit* outfit = nullptr;
        if (g_settings.reequipSleepOutfit && IsActorSleeping(actor)) {
            outfit = actorBase->sleepOutfit;
        }

        if (!outfit) {
            outfit = actorBase->defaultOutfit;
        }

        return outfit;
    }

    [[nodiscard]] bool ShouldSkipActor(RE::Actor* actor)
    {
        if (!actor) {
            return true;
        }

        if (actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled()) {
            return true;
        }

        if (!actor->Is3DLoaded()) {
            return true;
        }

        if (!actor->GetActorBase()) {
            return true;
        }

        if (g_settings.skipActorsInCombat && actor->IsInCombat()) {
            return true;
        }

        if (!g_settings.includeFollowers && actor->IsPlayerTeammate()) {
            return true;
        }

        return false;
    }

    bool ReequipDefaultOutfit(RE::Actor* actor)
    {
        if (ShouldSkipActor(actor) || HasVisibleBodyArmor(actor)) {
            return false;
        }

        auto* outfit = PickOutfit(actor);
        if (!outfit) {
            return false;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return false;
        }

        bool equippedAny = false;
        for (auto* form : outfit->outfitItems) {
            auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
            if (!armor) {
                continue;
            }

            actor->AddObjectToContainer(armor, nullptr, 1, actor);
            equipManager->EquipObject(
                actor,
                armor,
                nullptr,
                1,
                nullptr,
                false,
                true,
                false,
                true);

            equippedAny = true;
        }

        if (!equippedAny) {
            return false;
        }

        if (!HasVisibleBodyArmor(actor) && g_settings.forceInventoryReset) {
            spdlog::warn(
                "Body slot still empty after outfit equip, resetting inventory for {} ({:08X})",
                actor->GetName(),
                actor->GetFormID());
            actor->ResetInventory(false);

            auto* retryOutfit = PickOutfit(actor);
            if (retryOutfit) {
                for (auto* form : retryOutfit->outfitItems) {
                    auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
                    if (!armor) {
                        continue;
                    }

                    actor->AddObjectToContainer(armor, nullptr, 1, actor);
                    equipManager->EquipObject(
                        actor,
                        armor,
                        nullptr,
                        1,
                        nullptr,
                        false,
                        true,
                        false,
                        true);
                }
            }
        }

        spdlog::info(
            "Re-equipped outfit on {} ({:08X}), bodyArmorNow={}",
            actor->GetName(),
            actor->GetFormID(),
            HasVisibleBodyArmor(actor));

        return HasVisibleBodyArmor(actor);
    }

    template <class THandleArray>
    void ScanActorArray(const THandleArray& handles, std::uint32_t& repairedCount)
    {
        for (const auto& handle : handles) {
            auto actor = handle.get();
            if (!actor) {
                continue;
            }

            if (ReequipDefaultOutfit(actor.get())) {
                ++repairedCount;
            }
        }
    }

    void ScanActors()
    {
        std::scoped_lock lock(g_scanLock);

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return;
        }

        std::uint32_t repairedCount = 0;

        ScanActorArray(processLists->highActorHandles, repairedCount);
        ScanActorArray(processLists->middleHighActorHandles, repairedCount);
        ScanActorArray(processLists->middleLowActorHandles, repairedCount);
        ScanActorArray(processLists->lowActorHandles, repairedCount);

        if (repairedCount > 0) {
            spdlog::info("NPCOutfitFixer repaired {} actor(s) this pass", repairedCount);
        }
    }

    struct PlayerCharacterUpdateHook
    {
        static void thunk(RE::PlayerCharacter* player, float delta)
        {
            func(player, delta);

            const auto now = NowMs();
            const auto last = g_lastScanTick.load();
            if (now - last < g_settings.scanIntervalMs) {
                return;
            }

            g_lastScanTick.store(now);
            ScanActors();
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InitializeLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            path = std::filesystem::current_path();
        }

        *path /= "NPCOutfitFixer.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }

    void Install()
    {
        if (g_started.exchange(true)) {
            return;
        }

        LoadSettings();

        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        PlayerCharacterUpdateHook::func = playerVtbl.write_vfunc(0xAD, PlayerCharacterUpdateHook::thunk);

        spdlog::info("NPCOutfitFixer initialized and player update hook installed");
    }

    void MessageHandler(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            Install();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            g_lastScanTick.store(0);
            ScanActors();
            break;
        default:
            break;
        }
    }
}

extern "C"
{
    __declspec(dllexport) SKSE::PluginVersionData SKSEPlugin_Version = []() {
        SKSE::PluginVersionData data{};
        data.PluginName("NPCOutfitFixer");
        data.AuthorName("OpenAI Codex");
        data.PluginVersion({ 1, 0, 0, 0 });
        data.UsesAddressLibrary(true);
        data.HasNoStructUse(true);
        data.CompatibleVersions({ REL::Version{ 1, 6, 1170, 0 } });
        return data;
    }();

    __declspec(dllexport) bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
    {
        if (pluginInfo) {
            pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
            pluginInfo->name = "NPCOutfitFixer";
            pluginInfo->version = 1;
        }

        return true;
    }

    __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse)
    {
        SKSE::Init(skse);
        InitializeLog();
        spdlog::info("NPCOutfitFixer loading");

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(MessageHandler)) {
            spdlog::critical("Failed to register message listener");
            return false;
        }

        return true;
    }
}
