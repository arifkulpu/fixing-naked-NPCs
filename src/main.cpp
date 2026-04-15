#include "PCH.h"
#include "VisibilityFixer.h"

void InitializeLog() {
    auto path = logger::log_directory();
    if (!path) return;

    *path /= "NPCFixer.log"sv;
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

    auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);

    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S] %v"s);
}

// MODERN SKSE EXPORTS (Skyrim AE 1.6.1170 için gerekli)
extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ 1, 0, 0, 0 });
    v.PluginName("NPC Fixer");
    v.AuthorName("Antigravity");
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });

    return v;
}();

namespace Console {
    bool FixNpcs(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::Script*, RE::ScriptLocals*, double&, std::uint32_t&) {
        VisibilityFixer::ProcessFixes(true);
        if (auto console = RE::ConsoleLog::GetSingleton()) {
            console->Print("NPC Fixer: Yakindaki tum actorler zorla yenilendi.");
        }
        return true;
    }

    void Register() {
        auto commands = RE::SCRIPT_FUNCTION::GetFirstConsoleCommand();
        if (!commands) {
            logger::error("Konsol komut tablosu bulunamadi!");
            return;
        }

        for (std::uint32_t i = 0; i < RE::SCRIPT_FUNCTION::Commands::kConsoleCommandsEnd; ++i) {
            auto& command = commands[i];
            if (command.functionName && std::string_view(command.functionName) == "ToggleNavMesh") {
                // Güvenli yazım (SafeWrite) kullanarak bellek korumasını aşalım
                const char* name = "fixnpcs";
                const char* sn = "fnp";
                const char* help = "Yakindaki sorunlu NPC'leri zorla yeniler.";
                
                // Pointer tipleri için uintptr_t üzerinden yazım yapmalıyız
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.functionName), reinterpret_cast<std::uintptr_t>(name));
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.shortName), reinterpret_cast<std::uintptr_t>(sn));
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.helpString), reinterpret_cast<std::uintptr_t>(help));
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.executeFunction), reinterpret_cast<std::uintptr_t>(FixNpcs));
                
                // KRİTİK: Çökme (CTD) önlemek için diğer alanları temizleyelim
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.numParams), static_cast<std::uint16_t>(0));
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.params), reinterpret_cast<std::uintptr_t>(nullptr));
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.compileFunction), reinterpret_cast<std::uintptr_t>(nullptr));
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.conditionFunction), reinterpret_cast<std::uintptr_t>(nullptr));
                
                // Referans fonksiyonu (targeted command) değil, global komut
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&command.referenceFunction), false);
                
                logger::info("Konsol komutu 'fixnpcs' stabil ayarlarla ToggleNavMesh uzerine kaydedildi.");
                break;
            }
        }
    }
}

void OnMessage(SKSE::MessagingInterface::Message* a_msg) {
    switch (a_msg->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        VisibilityFixer::Install();
        Console::Register();
        break;
    }
}

extern "C" __declspec(dllexport) bool __cdecl SKSEPlugin_Load(const SKSE::LoadInterface* a_skse) {
    InitializeLog();
    logger::info("NPC Fixer yukleniyor...");

    SKSE::Init(a_skse);

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        logger::error("Messaging interface kaydi basarisiz!");
        return false;
    }

    logger::info("NPC Fixer basariyla yuklendi!");

    return true;
}
