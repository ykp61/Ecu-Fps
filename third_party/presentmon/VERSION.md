# Intel PresentMon - PresentData (vendored)

| Alan | Değer |
|---|---|
| Kaynak | https://github.com/GameTechDev/PresentMon |
| Etiket | `v2.6.0` (yayın tarihi 2026-09-21) |
| Commit | `e13fce6acdb55a808fd8318175a56863e532d95f` |
| Lisans | MIT (`LICENSE.txt`, upstream kökünden değiştirilmeden) |
| Alınma tarihi | 2026-11-04 (MEAS-01) |
| Derleme | `ef_presentdata` statik kütüphanesi (`CMakeLists.txt`); servis, ek EXE, vcpkg veya ağ erişimi yok |

## Değiştirilmeden kopyalananlar

Upstream deposundaki git blob karmaları ile birebir aynıdır (satır sonu repo kuralı gereği
LF olarak saklanır; upstream index de LF'dir). Doğrulama:
`git ls-files -s third_party/presentmon` çıktısındaki blob karmaları, upstream'de
`git ls-tree -r v2.6.0 -- PresentData IntelPresentMon/CommonUtilities LICENSE.txt` ile aynıdır.

- `LICENSE.txt`
- `PresentData/*.cpp, *.hpp, *.h` ve `PresentData/ETW/*.h` (tümü; `PresentData.vcxproj*` hariç)
- `IntelPresentMon/CommonUtilities/Meta.h`
- `IntelPresentMon/CommonUtilities/Hash.h`, `Hash.cpp`
- `IntelPresentMon/CommonUtilities/SampleStatistics.h`
- `IntelPresentMon/CommonUtilities/Qpc.h`, `Qpc.cpp`

**Upstream dosyalarda yapılan değişiklik: yok.** Derlenen kaynaklar: `Debug.cpp`, `GpuTrace.cpp`,
`InterPresentActivity.cpp`, `NvidiaTraceConsumer.cpp`, `PresentMonTraceConsumer.cpp`,
`PresentMonTraceSession.cpp`, `TraceConsumer.cpp`, `TraceLogging.cpp`, `Hash.cpp`, `Qpc.cpp`.
`ConsoleAdapter.h` (`pmon::util::metrics`) yalnız PresentMon konsol uygulamasında kullanılır;
kopyalandı ama hiçbir derlenen dosya onu dahil etmez.

## Shim (Ecu Fps kodu, upstream arayüzünden türetilmiş)

PresentData `../IntelPresentMon/CommonUtilities/...` başlıklarını dahil eder. Upstream bu
başlıklar üzerinden cereal, moodycamel concurrentqueue, PresentMonAPI2 ve büyük bir günlük
sistemi çeker; bunlar taşınmadı. Yerine yalnız PresentData'nın kullandığı semboller sağlandı:

| Dosya | Sağlanan | Not |
|---|---|---|
| `log/Log.h` | `pmon::util::log::Level`, `GlobalPolicy::Get().GetLogLevel()`, `EntryBuilder` (`note/watch/hr/code/first/every/diag`), `Voidifier_`, `pmlog_`, `pmlog_fatal/error/warn/info/dbg`, `pmwatch` | Makro biçimi upstream ile aynı (`koşul ? (void)0 : Voidifier & builder.note(...)`). Seviye sabit `None`: argümanlar derlenir ama günlük dalı çalışmaz, çıktı yok. `PMTraceSession::ProcessEtwEventLatencyStats` bu nedenle istatistik toplamaz. |
| `Exception.h/.cpp` | `pmon::util::Exception`, `Except<E>(...)`, `ReportException(note, eptr)`, `PM_DEFINE_EX`, `PM_DEFINE_EX_FROM`, `pmquell`, `pmcatch_report` | Yığın izi ve `PM_STATUS` yok; `ReportException` yalnız metin döner. |
| `str/String.h/.cpp` | `pmon::util::str::ToNarrow` | UTF-16 -> UTF-8 (`WideCharToMultiByte`). |
| `PrecisionWaiter.h/.cpp` | `pmon::util::PrecisionWaiter` (aynı public API) | Upstream mantığı (yüksek çözünürlüklü waitable timer + QPC spin, hata durumunda `Sleep`); `win::Handle`/günlük yerine ham `HANDLE`. Yalnız olay hızlandırma (`mPaceEvents`, ETL oynatma) kullanır; gerçek zamanlı oturumda kapalı. |
| `win/WinAPI.h` | `windows.h` | Upstream `_WIN32_WINNT=0x0603` zorlar; shim proje değerlerini (Windows 10) korur. Upstream `Hash.cpp` ve `Qpc.cpp` bunu dahil eder. |

`pmon::util::hash`, `FunctionPtrTraits`, `DependentFalse`, `SampleStatistics` ve QPC işlevleri
(`GetCurrentTimestamp`, `GetTimestampPeriodSeconds`, `TimestampDeltaToSeconds`) upstream
dosyalarından değiştirilmeden gelir.

## Derleme ayarları (yalnız bu hedef)

- Tanımlar: `UNICODE _UNICODE WIN32_LEAN_AND_MEAN _LIB`, `_WIN32_WINNT=0x0A00 WINVER=0x0A00
  NTDDI_VERSION=0x0A000000` (upstream vcxproj 0x0601 kullanır; proje Windows 10 hedefler).
- `PRESENTMON_ENABLE_DEBUG_TRACE=0`: Debug derlemede de ayrıntılı `wprintf` izi ve `assert()`
  devre dışı; `DebugAssert` sessiz `pmlog_warn`'a düşer.
- `/W3 /WX- /wd4996 /EHsc /utf-8 /guard:cf`; proje `/W4 /WX` bu hedefe uygulanmaz. Başlıklar
  tüketicilere `SYSTEM` include olarak verilir.
- CRT: proje genelindeki `CMAKE_MSVC_RUNTIME_LIBRARY` (statik `/MT`, `/MTd`) — upstream de statik.
- Bağlantı: `tdh`, `advapi32` (Windows sistem kütüphaneleri).

## Güncelleme kuralı

Yeni sürüm alınırken: etiket + commit sabitlenir, upstream dosyaları değiştirilmeden kopyalanır,
blob karmaları yeniden karşılaştırılır, PresentData'nın yeni `CommonUtilities` kullanımları
shim'e eklenir ve bu dosya güncellenir. Upstream'den hiçbir betik çalıştırılmaz.
