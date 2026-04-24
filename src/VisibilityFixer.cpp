#include "PCH.h"
#include "VisibilityFixer.h"
#include "Settings.h"

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

    // Aktör bazlı son fix zamanları
    std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> actorFixTimestamps;
    
    // Aktör bazlı aktif sahne zamanlayıcısı (en son sahneye girdiği an)
    // Bu zamanlayıcı, aktör sahnedeyken sürekli güncellenir ve sahne bitince de
    // bir süre daha geçerli kalır (SexLab stage transition koruması).
    std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> actorSceneActiveUntil;

    // Aktif SexLab sahnesindeki aktör FormID'leri
    std::unordered_set<RE::FormID> sexlabSceneActors;
    
    // OStim global takibi
    int ostimActiveThreadCount = 0;
    std::chrono::steady_clock::time_point ostimGlobalGraceTimer;

    // SexLab global takibi
    // sexlabActiveThreadCount > 0  →  en az bir thread aktif
    // sexlabGlobalGraceTimer       →  son olaydan itibaren güvenli pencere
    int sexlabActiveThreadCount = 0;
    std::chrono::steady_clock::time_point sexlabGlobalGraceTimer;

    // İşlem kuyruğu (Spam engelleme ve performans için)
    std::vector<PendingFix> pendingFixQueue;

    // Oyun başlangıç zamanı (Yeni oyun/yükleme sonrası stabilizasyon için)
    std::chrono::steady_clock::time_point systemStartTime = std::chrono::steady_clock::now();

    // -----------------------------------------------------------------------
    // Aktörün SexLab/OStim sahnesinde olup olmadığını döndürür.
    // Bu fonksiyon hem FixActor hem ProcessActorFix'te çağrılır.
    // -----------------------------------------------------------------------
    bool IsActorInScene(RE::Actor* a_actor)
    {
        auto now = std::chrono::steady_clock::now();

        // 1. Global OStim koruması
        if (ostimActiveThreadCount > 0 || now < ostimGlobalGraceTimer) {
            return true;
        }
        
        // 2. Global SexLab koruması (thread sayısı veya grace timer)
        if (sexlabActiveThreadCount > 0 || now < sexlabGlobalGraceTimer) {
            return true;
        }

        if (!a_actor) return false;

        // 3. Per-aktör SexLab sahne süresi dolmadı mı?
        auto formID = a_actor->GetFormID();
        auto itScene = actorSceneActiveUntil.find(formID);
        if (itScene != actorSceneActiveUntil.end() && now < itScene->second) {
            return true;
        }

        // 4. OStim Graph Değişkenleri
        bool ostimActive = false;
        if (a_actor->GetGraphVariableBool("OActive", ostimActive) && ostimActive) return true;
        
        std::int32_t oStatus = 0;
        if (a_actor->GetGraphVariableInt("OStatus", oStatus) && oStatus > 0) return true;

        // 5. SexLab Graph Değişkenleri
        bool sexlabActive = false;
        
        if (a_actor->GetGraphVariableBool("SexLabActive", sexlabActive) && sexlabActive) return true;
        if (a_actor->GetGraphVariableBool("bIsAnimating", sexlabActive) && sexlabActive) return true;
        if (a_actor->GetGraphVariableBool("SexLabLocked", sexlabActive) && sexlabActive) return true;
        if (a_actor->GetGraphVariableBool("bSexLabIsAnimating", sexlabActive) && sexlabActive) return true;
        
        std::int32_t threadID = 0;
        if (a_actor->GetGraphVariableInt("SexLabThreadID", threadID) && threadID > 0) return true;
        
        std::int32_t stageIndex = -1;
        if (a_actor->GetGraphVariableInt("SexLabStageIndex", stageIndex) && stageIndex >= 0) return true;

        return false;
    }

    // -----------------------------------------------------------------------
    // ModCallback Olay İşleyici
    // -----------------------------------------------------------------------
    class SceneEventHandler : public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            auto now = std::chrono::steady_clock::now();
            
            std::string evName = a_event->eventName.c_str();
            std::string evLower = evName;
            std::transform(evLower.begin(), evLower.end(), evLower.begin(), [](unsigned char c){ return std::tolower(c); });

            // --- OStim Olayları ---
            if (evLower.find("ostim") != std::string::npos) {
                if (evLower.find("start") != std::string::npos) {
                    ostimActiveThreadCount++;
                    ostimGlobalGraceTimer = now + 20s;
                    logger::info("VisibilityFixer: OStim sahnesi basladi ({}). Aktif: {}", evName, ostimActiveThreadCount);
                } 
                else if (evLower.find("end") != std::string::npos) {
                    if (evLower == "ostim_totalend") {
                        ostimActiveThreadCount = 0;
                        ostimGlobalGraceTimer = now + 30s;
                        logger::info("VisibilityFixer: OStim TotalEnd. Koruma 30s.");
                    } else {
                        if (ostimActiveThreadCount > 0) ostimActiveThreadCount--;
                        ostimGlobalGraceTimer = now + 20s;
                        logger::info("VisibilityFixer: OStim sahnesi bitti ({}). Kalan: {}", evName, ostimActiveThreadCount);
                    }
                }
            }
            
            // --- SexLab Olayları ---
            // SexLab'ın gönderdiği olayların büyük çoğunluğu "SexLab" veya "sl_" ile başlar.
            // Papyrus tabanlı stage geçişleri ise genelde "HookAnimation", "HookStage" kelimelerini içerir.
            else if (evLower.find("sexlab") != std::string::npos || evLower.rfind("sl_", 0) == 0 ||
                     evLower.find("hookanimation") != std::string::npos || evLower.find("hookstage") != std::string::npos || evLower.find("hookorgasm") != std::string::npos) {

                // ── Sahne tamamen bitti ──────────────────────────────────────
                bool isHardEnd = (evLower == "sexlabsceneend"   || evLower == "sexlab_scene_end"  ||
                                  evLower == "sexlabthreadend"  || evLower == "sexlab_thread_end" ||
                                  evLower == "sexlabend"        || evLower == "sexlab_end"        ||
                                  evLower == "sl_animend"       || evLower.find("hookanimationend") != std::string::npos);

                if (isHardEnd) {
                    // Thread sayacını sıfırla; timer'ı 45s cömert tut.
                    // SexLab kıyafetleri sahne bitiminde kendisi giydirir,
                    // dolayısıyla o pencerede bizim pluginimiz müdahale etmemeli.
                    sexlabActiveThreadCount = 0;
                    sexlabGlobalGraceTimer  = now + 45s;
                    sexlabSceneActors.clear();
                    logger::info("VisibilityFixer: SexLab sahnesi tamamen bitti ({}). 45sn gecis korumasi.", evName);
                }
                // ── Sahne başladı ────────────────────────────────────────────
                else if (evLower.find("start") != std::string::npos) {
                    if (sexlabActiveThreadCount == 0) sexlabActiveThreadCount = 1;
                    sexlabGlobalGraceTimer = now + 60s;
                    logger::info("VisibilityFixer: SexLab baslangic olayi ({}). Koruma 60s.", evName);
                }
                // ── Aşama geçişi / ara olaylar ──────────────────────────────
                // Bu dal özellikle StageChange, AnimationChange, AnimEnd gibi
                // olayları kapsar; sahne devam ediyor demektir.
                else {
                    if (sexlabActiveThreadCount == 0) sexlabActiveThreadCount = 1;
                    // Geçiş sırasında timer'ı 60 saniyeye yenile.
                    // 60s hem mevcut sahne süresi hem de geçiş boşluğunu kapsar.
                    sexlabGlobalGraceTimer = now + 60s;
                    logger::info("VisibilityFixer: SexLab gecis/ara olayi ({}). Koruma 60s.", evName);
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }

        static SceneEventHandler* GetSingleton()
        {
            static SceneEventHandler singleton;
            return std::addressof(singleton);
        }
    };

    // -----------------------------------------------------------------------

    bool IsHumanoidNPC(RE::Actor* a_actor)
    {
        if (!a_actor) return false;
        
        auto race = a_actor->GetRace();
        if (!race) return false;

        if (race->HasKeywordString("ActorTypeNPC") || race->HasKeywordString("ActorTypeHumanoid")) {
            return true;
        }

        if (race->data.flags.any(RE::RACE_DATA::Flag::kPlayable, RE::RACE_DATA::Flag::kFaceGenHead)) {
            return true;
        }

        auto npcBase = a_actor->GetActorBase();
        if (npcBase && (npcBase->defaultOutfit || npcBase->sleepOutfit)) {
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Kuyruktaki aktörü gerçekten düzelten fonksiyon.
    // ÖNEMLİ: Bu noktada da IsActorInScene kontrolü yapılır; çünkü aktör
    // 0.2s bekleme süresi içinde sahneye girmiş / hâlâ sahnede olabilir.
    // -----------------------------------------------------------------------
    void ProcessActorFix(RE::Actor* actor)
    {
        if (!actor || actor->IsDeleted() || !actor->Is3DLoaded()) return;
        if (actor->GetFormType() != RE::FormType::ActorCharacter) return;

        // ── Sahne koruması (kuyruğa eklendiğinden bu yana durum değişmiş olabilir) ──
        if (IsActorInScene(actor)) {
            logger::info("[Düzeltme] {} ({:08X}) kuyruktan çıkarıldı: sahne devam ediyor.", actor->GetName(), actor->GetFormID());
            return;
        }

        auto root   = actor->Get3D();
        auto formID = actor->GetFormID();

        logger::info("[Düzeltme] {} ({:08X}) analizi basladi...", actor->GetName(), formID);

        bool fixed = false;

        // 1. Görünmezlik (Ghost / Görünmez Ceset) Düzeltmesi
        if (!root) {
            if (actor->IsDead()) {
                logger::info(" -> Görünmez Ceset tespit edildi. Load3D zorlanıyor...");
            } else {
                logger::info(" -> Görünmezlik (Ghost) tespit edildi. Load3D zorlanıyor...");
            }
            
            actor->formFlags &= ~RE::TESForm::RecordFlags::kDisabled;
            actor->formFlags &= ~RE::TESObjectREFR::RecordFlags::kInitiallyDisabled;
            actor->Load3D(false);
            fixed = true;
        } 
        // 2. Çıplaklık ve Kıyafet Düzeltmesi (Eğer ayarlardan açıksa)
        else if (Settings::GetSingleton().fixNakedness) {
            if (IsHumanoidNPC(actor)) {
                RE::TESObjectARMO* chestArmor = nullptr;
                if (actor->GetActorRuntimeData().currentProcess) {
                    chestArmor = actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kBody);
                }

                if (!chestArmor && !actor->IsDead()) {
                    logger::info(" -> Çıplaklık tespit edildi. Envanter taranıyor...");
                    
                    auto inv = actor->GetInventory();
                    for (auto& [item, data] : inv) {
                        if (item && item->IsArmor()) {
                            auto armor = item->As<RE::TESObjectARMO>();
                            if (armor && armor->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kBody)) {
                                logger::info(" -> Envanterde zırh bulundu: {}. Giydiriliyor...", armor->GetName());
                                RE::ActorEquipManager::GetSingleton()->EquipObject(actor, armor, nullptr, 1, nullptr, true, false, false, false);
                                fixed = true;
                                break;
                            }
                        }
                    }

                    if (!fixed) {
                        auto npcBase = actor->GetActorBase();
                        if (npcBase && (npcBase->defaultOutfit || npcBase->sleepOutfit)) {
                            logger::info(" -> Outfit tanımlı. Envanter sıfırlanıyor...");
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
        } else {
            // Çıplaklık düzeltmesi kapalı olsa bile Başsızlık kontrolünü yapalım
            if (IsHumanoidNPC(actor)) {
                if (!root->GetObjectByName("FaceGenNiNode")) {
                    logger::info(" -> Başsızlık düzeltiliyor...");
                    fixed = true;
                }
            }
        }

        if (fixed) {
            actor->Update3DModel();
            actor->UpdateAnimation(0.0f);
            logger::info(" -> Düzeltme işlemi tamamlandı.");
        } else {
            logger::info(" -> Düzeltme gerekmedi veya yapılamadı.");
        }
    }

    // -----------------------------------------------------------------------
    // Tek bir aktörü kontrol eder; sorun varsa kuyruğa ekler.
    // -----------------------------------------------------------------------
    void FixActor(RE::Actor* actor, std::chrono::steady_clock::time_point now, bool a_force)
    {
        if (!actor || actor->IsPlayerRef() || actor->IsDeleted()) return;
        if (actor->IsDisabled()) return;

        auto formID = actor->GetFormID();

        // ── Sahne / geçiş koruması ────────────────────────────────────────────
        if (IsActorInScene(actor)) {
            // Aktörün "sahne aktif" süresini sürekli tazele.
            // SexLab olayları gelene kadar per-aktör grace = 90 saniye.
            actorSceneActiveUntil[formID] = now + 90s;
            return;
        }

        // Per-aktör grace timer hâlâ geçerli mi?
        {
            auto it = actorSceneActiveUntil.find(formID);
            if (it != actorSceneActiveUntil.end() && now < it->second) {
                // Sahne bitmedi ya da çok yakın zamanda bitti — müdahale etme.
                return;
            }
        }
        // ─────────────────────────────────────────────────────────────────────

        if (!actor->Is3DLoaded() || actor->GetFormType() != RE::FormType::ActorCharacter) return;

        // Cooldown kontrolü
        auto it = actorFixTimestamps.find(formID);
        if (!a_force && it != actorFixTimestamps.end()) {
            if (now - it->second < std::chrono::seconds(Settings::GetSingleton().cooldown)) {
                return;
            }
        }

        bool needsFix = false;

        // --- Görünmezlik Kontrolü ---
        if (actor->Get3D() == nullptr) {
            logger::info("[Görünmezlik] {} ({:08X}) tespit edildi.", actor->GetName(), formID);
            needsFix = true;
        }

        // --- Başsızlık / Çıplaklık ---
        if (!needsFix) {
            auto root = actor->Get3D();
            if (root) {
                if (!root->GetObjectByName("FaceGenNiNode")) {
                    logger::info("[Başsızlık] {} ({:08X}) tespit edildi.", actor->GetName(), formID);
                    needsFix = true;
                }
                
                if (!needsFix && Settings::GetSingleton().fixNakedness && IsHumanoidNPC(actor) && !actor->IsDead()) {
                    RE::TESObjectARMO* chestArmor = nullptr;
                    if (actor->GetActorRuntimeData().currentProcess) {
                        chestArmor = actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kBody);
                    }
                    
                    if (!chestArmor) {
                        logger::info("[Çıplaklık] {} ({:08X}) tespit edildi.", actor->GetName(), formID);
                        needsFix = true;
                    }
                }
            }
        }

        if (needsFix) {
            PendingFix pending;
            pending.actorHandle = actor->GetHandle();
            pending.fixTime     = now + 200ms;
            
            pendingFixQueue.push_back(pending);
            actorFixTimestamps[formID] = now;
            
            if (Settings::GetSingleton().logFixes) {
                logger::info(" -> Aktör kuyruğa eklendi ({:08X}), 0.2s sonra düzeltilecek.", formID);
            }
        }
    }

    // -----------------------------------------------------------------------

    void ProcessFixes(bool a_force)
    {
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->GameIsPaused()) return;

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
        
        if (now - systemStartTime < std::chrono::seconds(Settings::GetSingleton().startupDelay)) {
            return;
        }

        ProcessQueue(now);

        static auto lastUpdate = now;
        if (now - lastUpdate > std::chrono::seconds(Settings::GetSingleton().scanInterval)) {
            ProcessFixes(false);
            lastUpdate = now;
        }
    }

    void ClearFixedActors()
    {
        actorFixTimestamps.clear();
        actorSceneActiveUntil.clear();
        sexlabSceneActors.clear();
        pendingFixQueue.clear();
        ostimActiveThreadCount  = 0;
        sexlabActiveThreadCount = 0;
        systemStartTime = std::chrono::steady_clock::now();
        logger::info("VisibilityFixer: Tum listeler ve zamanlayicilar sifirlandi.");
    }

    // -----------------------------------------------------------------------
    // Ana döngü kancası
    // -----------------------------------------------------------------------
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
        REL::Relocation<std::uintptr_t> target{
            REL::RelocationID(35551, 36544),
            static_cast<std::ptrdiff_t>(REL::VariantOffset(0x11F, 0x160, 0x11F).offset())
        };
        
        SKSE::AllocTrampoline(14);
        MainUpdateHook::func = SKSE::GetTrampoline().write_call<5>(target.address(), MainUpdateHook::thunk);
        
        auto modCallbackSource = SKSE::GetModCallbackEventSource();
        if (modCallbackSource) {
            modCallbackSource->AddEventSink(SceneEventHandler::GetSingleton());
            logger::info("VisibilityFixer: ModCallback olaylari (OStim/SexLab) kaydedildi.");
        }

        logger::info("VisibilityFixer: Sistem basariyla yuklendi.");
    }
}
