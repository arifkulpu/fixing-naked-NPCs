#pragma once

#include "PCH.h"
#include <string>
#include <Windows.h>

class Settings {
public:
    static Settings& GetSingleton() {
        static Settings instance;
        return instance;
    }

    void Load() {
        constexpr auto path = L"Data/SKSE/Plugins/NakedNPCFix.ini";

        scanInterval = GetPrivateProfileIntW(L"General", L"fScanInterval", 5, path);
        cooldown = GetPrivateProfileIntW(L"General", L"fCooldown", 60, path);
        startupDelay = GetPrivateProfileIntW(L"General", L"fStartupDelay", 10, path);
        fixNakedness = GetPrivateProfileIntW(L"General", L"bFixNakedness", 1, path) != 0;
        
        logFixes = GetPrivateProfileIntW(L"Debug", L"bLogFixes", 1, path) != 0;
        verboseLogging = GetPrivateProfileIntW(L"Debug", L"bVerboseLogging", 0, path) != 0;

        logger::info("Ayarlar yuklendi:");
        logger::info(" -> Tarama Araligi: {}s", scanInterval);
        logger::info(" -> Cooldown: {}s", cooldown);
        logger::info(" -> Baslangic Gecikmesi: {}s", startupDelay);
        logger::info(" -> Ciplaklik Duzeltmesi: {}", fixNakedness ? "Acik" : "Kapali");
        logger::info(" -> Detayli Log: {}", logFixes ? "Acik" : "Kapali");
    }

    std::uint32_t scanInterval{ 5 };
    std::uint32_t cooldown{ 60 };
    std::uint32_t startupDelay{ 10 };
    bool fixNakedness{ true };
    bool logFixes{ true };
    bool verboseLogging{ false };

private:
    Settings() = default;
};
