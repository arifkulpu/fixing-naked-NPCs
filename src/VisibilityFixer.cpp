#include "PCH.h"
#include "VisibilityFixer.h"

#include <unordered_set>
#include <vector>
#include <chrono>

using namespace std::chrono_literals;

namespace VisibilityFixer
{
    // Bekleyen düzeltme işlemleri için yapı (Performans için kademeli işleme)
    struct PendingFix
    {
        RE::ObjectRefHandle actorHandle;
        std::chrono::steady_clock::time_point fixTime;
    };

    // Aktör bazlı son fix zamanları (Performansı korumak ama hatalı fixleri tekrar denemek için 60sn cooldown)
    std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> actorFixTimestamps;
    
    // İşlem kuyruğu (Spam engelleme ve performans için)
    std::vector<PendingFix> pendingFixQueue;

    // Oyun başlangıç zamanı (Yeni oyun/yükleme sonrası stabilizasyon için)
    std::chrono::steady_clock::time_point systemStartTime = std::chrono::steady_clock::now();

    void ClearFixedActors()
    {
        actorFixTimestamps.clear();
        pendingFixQueue.clear();
        systemStartTime = std::chrono::steady_clock::now(); // Zamanlayıcıyı sıfırla
        logger::info("VisibilityFixer: Fix listesi ve baslangic zamanlayicisi sifirlandi.");
    }

    void ProcessActorFix(RE::Actor* actor)
    {
        if (!actor) return;
        
        auto formID = actor->GetFormID();
        auto root = actor->Get3D();
        
        logger::info("[Düzeltme] {} ({:08X}) analizi basladi...", actor->GetName(), formID);

        bool fixed = false;

        // 1. Görünmezlik (Ghost / Görünmez Ceset) Düzeltmesi
        if (!root) {
            if (actor->IsDead()) {
                logger::info(" -> Görünmez Ceset (Ghost Corpse) tespit edildi. Load3D zorlanıyor...");
            } else {
                logger::info(" -> Görünmezlik (Ghost) tespit edildi. Load3D zorlanıyor...");
            }
            
            // kDisabled ve kInitiallyDisabled flag'lerini temizle (Flicker engelleme)
            actor->formFlags &= ~RE::TESForm::RecordFlags::kDisabled;
            actor->formFlags &= ~RE::TESObjectREFR::RecordFlags::kInitiallyDisabled;
            
            // Motoru modeli yeniden yüklemeye zorla
            actor->Load3D(false);
            
            fixed = true;
        } 
        // 2. Çıplaklık ve Kıyafet Düzeltmesi
        else {
            auto race = actor->GetRace();
            bool isNPC = race && race->HasKeywordString("ActorTypeNPC");
            
            if (isNPC) {
                // Vücut zırhı var mı?
                auto chestArmor = actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kBody);
                if (!chestArmor) {
                    logger::info(" -> Çıplaklık tespit edildi. Envanter taranıyor...");
                    
                    // Envanterde vücut zırhı var mı bak
                    auto inv = actor->GetInventory();
                    for (auto& [item, data] : inv) {
                        if (item && item->IsArmor()) {
                            auto armor = item->As<RE::TESObjectARMO>();
                            if (armor && armor->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kBody)) {
                                logger::info(" -> Envanterde zırh bulundu: {}. Giydiriliyor...", armor->GetName());
                                actor->AddWornItem(armor, 1, true, 0, 0);
                                fixed = true;
                                break;
                            }
                        }
                    }

                    // Envanterde yoksa Outfit'i sıfırla
                    if (!fixed) {
                        auto npcBase = actor->GetActorBase();
                        if (npcBase && (npcBase->defaultOutfit || npcBase->sleepOutfit)) {
                            logger::info(" -> Outfit tanımlı. Envanter sıfırlanarak kıyafetler zorlanıyor...");
                            actor->ResetInventory(false);
                            fixed = true;
                        }
                    }
                }

                // Başsızlık kontrolü
                if (!root->GetObjectByName("FaceGenNiNode")) {
                    logger::info(" -> Başsızlık düzeltiliyor...");
                    fixed = true;
                }
            }
        }

        if (fixed) {
            actor->Update3DModel();
            actor->UpdateAnimation(0.0f); // Animasyon sistemini tetikleyerek görünürlüğü garanti altına al
            logger::info(" -> Düzeltme işlemi tamamlandı.");
        } else {
            logger::info(" -> Düzeltme gerekmedi veya yapılamadı.");
        }
    }

    void FixActor(RE::Actor* actor, std::chrono::steady_clock::time_point now, bool a_force)
    {
        if (!actor || actor->IsPlayerRef()) return;
        if (actor->IsDisabled()) return; // Sadece tamamen devredışı kalanları atla, ölüleri (cesetleri) atlama
        
        // 3D yüklü değilse işlem yapma (3D check doğru yap kuralı)
        if (!actor->Is3DLoaded()) return;

        auto formID = actor->GetFormID();

        // 60 saniyelik bekleme süresi kontrolü (Performans ve tekrar deneme dengesi)
        auto it = actorFixTimestamps.find(formID);
        if (!a_force && it != actorFixTimestamps.end()) {
            if (now - it->second < 60s) {
                return;
            }
        }

        bool needsFix = false;

        // --- GÖRÜNMEZLİK (GHOST) KONTROLÜ ---
        if (actor->Get3D() == nullptr) {
            logger::info("[Görünmezlik] {} ({:08X}) tespit edildi.", actor->GetName(), formID);
            needsFix = true;
        }

        // --- ÇIPLAKLIK VE BAŞSIKLIK KONTROLLERİ ---
        if (!needsFix) {
            auto root = actor->Get3D();
            if (root) {
                // Başsızlık kontrolü
                if (!root->GetObjectByName("FaceGenNiNode")) {
                    logger::info("[Başsızlık] {} ({:08X}) tespit edildi.", actor->GetName(), formID);
                    needsFix = true;
                }
                
                // Çıplaklık kontrolü (NPC ise)
                if (!needsFix) {
                    auto race = actor->GetRace();
                    if (race && race->HasKeywordString("ActorTypeNPC")) {
                        auto chestArmor = actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kBody);
                        if (!chestArmor) {
                            logger::info("[Çıplaklık] {} ({:08X}) tespit edildi.", actor->GetName(), formID);
                            needsFix = true;
                        }
                    }
                }
            }
        }

        if (needsFix) {
            // Kuyruğa ekle (Aynı frame içinde spam engelleme ve 0.2s bekleme kuralı)
            PendingFix pending;
            pending.actorHandle = actor->GetHandle();
            pending.fixTime = now + 200ms;
            
            pendingFixQueue.push_back(pending);
            actorFixTimestamps[formID] = now; // Fix zamanını kaydet
            
            logger::info(" -> Aktör kuyruğa eklendi, 0.2s sonra düzeltilecek.");
        }
    }

    void ProcessFixes(bool a_force)
    {
        // Menü veya yükleme ekranındaysak işlem yapma (Cell load koruması)
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->GameIsPaused()) {
            return;
        }

        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        auto now = std::chrono::steady_clock::now();

        for (auto& handle : processLists->highActorHandles) {
            auto actor = handle.get();
            if (actor) {
                FixActor(actor.get(), now, a_force);
            }
        }
    }

    void ProcessQueue(std::chrono::steady_clock::time_point now)
    {
        if (pendingFixQueue.empty()) return;

        for (auto it = pendingFixQueue.begin(); it != pendingFixQueue.end(); ) {
            auto ref = it->actorHandle.get();
            if (ref && now >= it->fixTime) {
                auto actor = ref->As<RE::Actor>();
                if (actor) {
                    ProcessActorFix(actor);
                }
                it = pendingFixQueue.erase(it);
            } else if (!ref) {
                it = pendingFixQueue.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Update()
    {
        auto now = std::chrono::steady_clock::now();
        
        // Stabilizasyon için ilk 10 saniye işlem yapma (Yeni oyun/Load sonrası)
        if (now - systemStartTime < 10s) {
            return;
        }

        // Kuyruğu her karede işle
        ProcessQueue(now);

        static auto lastUpdate = now;
        // 5 saniyede bir kontrol et (Update süresini büyüt kuralı)
        if (now - lastUpdate > 5s) {
            ProcessFixes(false);
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
