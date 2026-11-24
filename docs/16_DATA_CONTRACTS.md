# Veri sözleşmeleri — tasarım, henüz uygulama değil

Tüm kayıtlar schema_version taşır. Enum değerleri typed; bilinmeyen değer sessiz default olmaz. Birimler alan adında veya unit alanında açık; UTC zaman ve monoton capture clock ayrıdır. Eksik metrik null + reason; 0 ölçümüyle aynı değildir. Aşağıdaki yapı adları ilerideki kod için sözleşmedir.

| Model | Zorunlu alan grupları |
|---|---|
| DeviceContext | OS/build/edition/arch, user/session kapsamı, CPU/GPU/volume identity, driver, power context, inventory provenance |
| Capability | feature_id, device scope, supported/unsupported/unknown/denied, API/provider/version, reason, required privilege, validation evidence |
| SettingOperation | typed operation id/params, hedef scope, preconditions, snapshot codec, apply/verify/rollback, reboot/quality/conflict metadata |
| Transaction | transaction id, app/rule/schema, actor+target SID, ordered operations, durable intent/result, current lifecycle, recovery state |
| BenchmarkRun | run id, source app/game, scene+seed+workload version, settings fingerprint, hardware/software context, timeline, sample validity, results |
| MetricSample | source, timestamp domain, metric id, numeric value or null, unit, valid flag, missing reason, sampling interval |
| Diagnosis | scope/time window, category, component, evidence references, counterevidence, confidence+reason, limits, retest request |
| Experiment | baseline/change group ids, A/B run ids, same-condition checks, quality category, run-level statistics, decision and rollback result |
| SupportEvidence | feature, exact tested combination, result/status, artifact+commit, procedure, evidence path, reviewer note |
| PeerCohort | benchmark/version/tier/API, hardware classes, software context, sample provenance/count, exclusions, uncertainty, privacy policy |

## Geçerlilik ve sınırlar

Tüm dış JSON/IPC/dataset girdileri boyut/derinlik/string/range sınırından geçer. Metadata sahte işlem komutuna dönüşmez. Import dataset yalnız pasif veridir. Journal gizli/sensitive alanları export edilmez. DLL veya registry yollarını veri dosyasından yönetici komutu olarak kabul etme.

PID+creation time, adapter kimliği, volume identity, oyun config hash/sürümü ile hedef doğrulanır. Donanım UUID/seri numarası paylaşım için varsayılan alan değildir; yerel makine eşleme amacıyla kullanılan değer dışa aktarımdan çıkarılır.

## Gereken ayrım

Task status, feature readiness ve run validity farklı enum'lardır. Bir görevin done olması her cihazda verified_supported anlamına gelmez. UI'nın pending reboot/kısmi restore durumunu Açık/Kapalı alanıyla tek boolean'a indirgeme. Benchmark sonucu ile görüntü referansındaki temsili sayı ayrı veri sınıflarıdır.
