# NPC Fixer - Invisible and Naked NPC Fixer

An SKSE plugin for **Skyrim Anniversary Edition (1.6.1170)** that automatically detects and resolves common NPC rendering and equipment glitches.

---

### [English]

#### Features
- **Invisible NPC Fix**: Detects "ghost" NPCs (actors existing in the engine but not rendering) and forces their 3D models to reload.
- **Naked NPC Fix**: Scans for actors missing body armor/clothing. It checks both the outfit system and the actor's inventory to automatically re-equip missing items.
- **Headless NPC Fix**: Identifies actors with missing head models (FaceGen nodes) and refreshes them.
- **Console Command**: Adds `fixnpcs` (or `fnp`) command to manually refresh all nearby actors without waiting for the auto-scan.
- **High Performance**: Optimized loop running every 2 seconds on the main thread with a robust cooldown system to prevent performance impact.
- **Stability**: Specifically designed to be compatible with `ConsoleUtilSSE` and other console-hooking mods.

#### Requirements
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Skyrim AE 1.6.1170 (CommonLibSSE-NG based)

#### Installation
1. Download the `NakedNPCFix.dll`.
2. Place it in `Data/SKSE/Plugins/`.
3. Check `Documents/My Games/Skyrim Special Edition/SKSE/NPCFixer.log` for logs.

---

### [Türkçe]

#### Özellikler
- **Görünmez NPC Düzeltmesi**: Motor üzerinde var olan ancak görseli yüklenmeyen (hayalet) NPC'leri tespit eder ve modellerini yeniler.
- **Çıplak NPC Düzeltmesi**: Vücut zırhı veya kıyafeti eksik olan NPC'leri tarar. Hem kıyafet seti (outfit) sistemini hem de NPC'nin envanterini kontrol ederek eksik eşyaları otomatik giydirir.
- **Başsız NPC Düzeltmesi**: Kafa modeli (FaceGen) yüklenememiş aktörleri tespit eder ve 3D modellerini tazeler.
- **Konsol Komutu**: Otomatik taramayı beklemeden, konsola `fixnpcs` (veya `fnp`) yazarak yakındaki tüm aktörleri anında yenileyebilirsiniz.
- **Yüksek Performans**: Ana thread üzerinde her 2 saniyede bir çalışan, performans dostu ve cooldown sistemli optimizasyon.
- **Stabilite**: `ConsoleUtilSSE` ve diğer konsol modlarıyla tam uyumlu olacak şekilde geliştirilmiştir.

#### Kurulum
1. `NakedNPCFix.dll` dosyasını indirin.
2. `Data/SKSE/Plugins/` klasörüne kopyalayın.
3. `Documents/My Games/Skyrim Special Edition/SKSE/NPCFixer.log` dosyasından durumu takip edebilirsiniz.

---

## Build (Geliştiriciler İçin)
This project uses **vcpkg** and **CMake**.
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Credits
- Developed by Antigravity (Pair Programming with arifkulpu)
- Uses [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)
