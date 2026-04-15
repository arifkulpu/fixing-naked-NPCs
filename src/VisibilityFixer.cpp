#include "PCH.h"
#include "VisibilityFixer.h"

#include <map>
#include <chrono>

using namespace std::chrono_literals;

namespace VisibilityFixer
{
    // Fix tipleri
    enum class FixType
    {
        kInvisible,
        kNaked,
        kHeadless
    };

    // FormID -> (FixType -> Son Fix Zamanı)
    std::map<RE::FormID, std::map<FixType, std::chrono::steady_clock::time_point>> fixCooldowns;

    void FixActor(RE::Actor* actor, std::chrono::steady_clock::time_point now, bool a_force)
    {
        if (!actor || actor->IsPlayerRef()) return;
        if (actor->IsDisabled() || actor->IsDead()) return;

        auto formID = actor->GetFormID();

        // --- GÖRÜNMEZLİK (GHOST) KONTROLÜ ---
        if (actor->Is3DLoaded() && actor->Get3D() == nullptr) {
            auto& lastFix = fixCooldowns[formID][FixType::kInvisible];
            if (a_force || now - lastFix > 10s) {
                logger::info("[Görünmezlik] {} ({:08X}) tespit edildi. Yenileniyor...", actor->GetName(), formID);
                actor->formFlags &= ~RE::TESForm::RecordFlags::kDisabled;
                actor->Update3DModel();
                fixCooldowns[formID][FixType::kInvisible] = now;
                return;
            }
        }

        // 3D yüklü değilse diğer kontrollere gerek yok
        auto root = actor->Get3D();
        if (!root) return;

        // --- ÇIPLAKLIK (NAKED) KONTROLÜ ---
        auto race = actor->GetRace();
        bool isNPC = race && race->HasKeywordString("ActorTypeNPC");
        
        if (isNPC) {
            // Göğüs zırhı/kıyafeti var mı? (Slot 32)
            auto chestArmor = actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kBody);
            if (!chestArmor) {
                auto& lastFix = fixCooldowns[formID][FixType::kNaked];
                if (a_force || now - lastFix > 30s) {
                    logger::info("[Çıplaklık] {} ({:08X}) zırhsız tespit edildi. Analiz ediliyor...", actor->GetName(), formID);
                    
                    bool fixed = false;
                    auto npcBase = actor->GetActorBase();

                    // 1. Durum: Envanterinde kıyafet var mı ama giymiyor mu?
                    auto inv = actor->GetInventory();
                    for (auto& [item, data] : inv) {
                        if (item && item->IsArmor()) {
                            auto armor = item->As<RE::TESObjectARMO>();
                            if (armor && armor->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kBody)) {
                                logger::info(" -> Envanterde vücut zırhı bulundu: {}. Giydiriliyor...", armor->GetName());
                                actor->AddWornItem(armor, 1, true, 0, 0);
                                fixed = true;
                                break;
                            }
                        }
                    }

                    // 2. Durum: Outfit tanımlı mı?
                    if (!fixed && npcBase) {
                        auto outfit = npcBase->defaultOutfit;
                        if (!outfit) outfit = npcBase->sleepOutfit;

                        if (outfit) {
                            logger::info(" -> Outfit tanimli ({}). Envanter sifirlaniyor...", outfit->GetName());
                            actor->ResetInventory(false);
                            fixed = true;
                        }
                    }

                    if (fixed) {
                        actor->Update3DModel(); // Görsel yenilemeyi zorla
                        fixCooldowns[formID][FixType::kNaked] = now;
                    } else {
                        logger::info(" -> Atlandi: Gecerli outfit veya envanterde kiyafet bulunamadi.");
                    }
                }
            }

            // --- BAŞSIZLIK (HEADLESS) KONTROLÜ ---
            auto faceNode = root->GetObjectByName("FaceGenNiNode");
            if (!faceNode) {
                auto& lastFix = fixCooldowns[formID][FixType::kHeadless];
                if (a_force || now - lastFix > 20s) {
                    logger::info("[Başsızlık] {} ({:08X}) kafa modeli eksik! Yenileniyor...", actor->GetName(), formID);
                    actor->Update3DModel();
                    fixCooldowns[formID][FixType::kHeadless] = now;
                    return;
                }
            }
        }
    }

    void ProcessFixes(bool a_force)
    {
        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        auto now = std::chrono::steady_clock::now();

        for (auto& handle : processLists->highActorHandles) {
            auto actor = handle.get();
            if (actor) {
                FixActor(actor.get(), now, a_force);
            }
        }

        // Temizlik her 5 dk'da bir (a_force ise temizlik yapma)
        if (!a_force) {
            static auto lastCleanup = now;
            if (now - lastCleanup > 5min) {
                for (auto it = fixCooldowns.begin(); it != fixCooldowns.end(); ) {
                    bool allExpired = true;
                    for (auto& [type, time] : it->second) {
                        if (now - time < 5min) {
                            allExpired = false;
                            break;
                        }
                    }
                    if (allExpired) it = fixCooldowns.erase(it);
                    else ++it;
                }
                lastCleanup = now;
            }
        }
    }

    void Update()
    {
        static auto lastUpdate = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        // Performans için 2 saniyede bir kontrol et
        if (now - lastUpdate > 2s) {
            ProcessFixes();
            lastUpdate = now;
        }
    }

    // Ana döngü kancası
    struct MainUpdateHook
    {
        static void thunk(RE::Main* a_this, float a_delta)
        {
            func(a_this, a_delta);
            Update();
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    void Install()
    {
        // Main::Update (Skyrim AE 1.6.1170 için ID 35551, Offset 0x11F)
        REL::Relocation<std::uintptr_t> target{ REL::ID(35551), 0x11F }; 
        
        SKSE::AllocTrampoline(14);
        MainUpdateHook::func = SKSE::GetTrampoline().write_call<5>(target.address(), MainUpdateHook::thunk);
        
        logger::info("VisibilityFixer: Sistem basariyla yuklendi.");
    }
}
