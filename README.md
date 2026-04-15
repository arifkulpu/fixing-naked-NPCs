# NPCOutfitFixer

Skyrim Anniversary Edition `1.6.1170` icin hedeflenmis, `SKSE` tabanli bir NPC kiyafet onarma eklentisi.

Modun amaci, bazen yuklendikten sonra veya AI durum gecislerinde kiyafetsiz kalan NPC'leri tespit edip NPC'nin varsayilan outfit'inden uygun govde zirhini yeniden equip etmektir.

## Nasil calisir

- Oyun verileri yuklendikten sonra eklenti devreye girer.
- Kayit yukleme ve yeni oyun aninda bir tarama yapar.
- Menu kapanislari sonrasinda belirli araliklarla yuksek islemdeki NPC'leri tarar.
- Uzerinde `body` slotunu dolduran bir armor yoksa, NPC'nin `defaultOutfit` kaydindaki uygun zirhi yeniden giydirmeye calisir.
- NPC'nin envanterinde ilgili armor yoksa, bir kopya ekleyip equip eder.

Bu yontem `ResetInventory` kadar agresif degildir; AI paketlerini ve diger item durumlarini bozma riskini azaltir.

## Gereksinimler

- Visual Studio 2022
- CMake 3.21+
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)
- Skyrim AE 1.6.1170 uyumlu SKSE / Address Library kurulumu

## Derleme

Ornek configure:

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

`CommonLibSSE` paketini kendi ortamina gore `vcpkg` veya baska bir yolla saglayabilirsin.

## Kurulum

Derleme sonrasi olusan:

- `NPCOutfitFixer.dll`
- `NPCOutfitFixer.ini`

dosyalarini asagidaki konuma koy:

```text
Data/SKSE/Plugins/
```

## Ayarlar

`NPCOutfitFixer.ini`:

- `ScanIntervalMs`: Taramalar arasi bekleme suresi.
- `IncludeFollowers`: Takipcileri de kontrol et.
- `ReequipSleepOutfit`: NPC uyuyorsa `sleepOutfit` denemesi yap.
- `SkipActorsInCombat`: Savastaki NPC'leri atla.

## Notlar

- Bu iskelet, SKSE plugin gelistirmeye hizli baslaman icin hazirlandi.
- Kullandigin `CommonLibSSE-NG` surumune gore bazi API isimlerinde kucuk uyarlamalar gerekebilir.
- Istersen bir sonraki adimda buna `SPID` tabanli yedek kiyafet listesi, loglama seviyesi veya MCM destegi de ekleyebilirim.
