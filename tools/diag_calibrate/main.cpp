// DIAG-06/BM-02: kalibrasyon veri seti degerlendiricisi (gelistirici araci; pakete girmez).
// Kullanim: ef_diag_calibrate <dataset.json>
// Cikis kodlari: 0 = yayin kapisi acik, 3 = gecerli veri ama kapi kapali, 2 = okuma/bicim hatasi,
//               5 = bolme butunlugu hatasi (sizinti/yinelenmis makine).
// Deger seti gercek donanim etiketleri tasimiyorsa kapi ASLA acilmaz (sentetik fikstur gercek sayilmaz).

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "core/diagnosis/calibration.h"
#include "core/diagnosis/calibration_io.h"
#include "core/recovery/sha256.h"

namespace {

void print_stats(const char* name, const ef::core::diagnosis::CalibrationStats& s) {
    std::printf(
        "%s total=%zu correct=%zu fp=%zu fn=%zu abstain=%zu wrong=%zu synthetic=%zu real=%zu "
        "accuracy=%.4f answered_accuracy=%.4f coverage=%.4f\n",
        name, s.total, s.correct, s.false_positive, s.false_negative, s.abstained, s.wrong_class, s.synthetic,
        s.real, ef::core::diagnosis::accuracy_all(s), ef::core::diagnosis::accuracy_answered(s),
        ef::core::diagnosis::coverage(s));
}

void print_confusion(const char* name, const ef::core::diagnosis::CalibrationStats& s) {
    using namespace ef::core::diagnosis;
    for (std::size_t l = 0; l < kComputeClassCount; ++l) {
        const auto ln = to_string(static_cast<ComputeClass>(l));
        std::printf("%s confusion label=%.*s", name, static_cast<int>(ln.size()), ln.data());
        for (std::size_t p = 0; p < kComputeClassCount; ++p) std::printf(" %zu", s.confusion[l][p]);
        std::printf("\n");
    }
}
void print_model(const ef::core::diagnosis::ModelReport& m) {
    if (!m.scored) {
        std::printf("model %.*s scored=0 labeled=%zu reason=inputs_not_in_dataset\n", static_cast<int>(m.model.size()),
                    m.model.data(), m.labeled);
        return;
    }
    const auto& s = m.stats;
    const double fp = s.total == 0 ? 0.0 : static_cast<double>(s.false_positive) / static_cast<double>(s.total);
    std::printf("model %.*s scored=1 labeled=%zu fp_rate=%.4f coverage=%.4f abstain=%zu out_of_scope=%zu\n",
                static_cast<int>(m.model.size()), m.model.data(), m.labeled, fp, ef::core::diagnosis::coverage(s),
                s.abstained, s.out_of_scope);
}

}  // namespace

bool valid_date(std::string_view s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
    return true;
}

int main(int argc, char** argv) {
    using namespace ef::core::diagnosis;
    std::string path;
    bool emit_header = false;
    std::string collected_on;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--emit-header") {
            emit_header = true;
        } else if (a == "--collected-on" && i + 1 < argc) {
            collected_on = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "usage: ef_diag_calibrate <dataset.json> [--emit-header --collected-on YYYY-MM-DD]\n");
            return 2;
        } else {
            path = std::string(a);
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "usage: ef_diag_calibrate <dataset.json> [--emit-header --collected-on YYYY-MM-DD]\n");
        return 2;
    }
    if (emit_header && !valid_date(collected_on)) {
        std::fprintf(stderr, "error: --emit-header requires --collected-on YYYY-MM-DD\n");
        return 2;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "error: cannot open %s\n", path.c_str());
        return 2;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto doc = parse_calibration(text);
    if (!doc.ok()) {
        std::fprintf(stderr, "error: %.*s: %s\n",
                     static_cast<int>(ef::contracts::to_string(doc.code()).size()),
                     ef::contracts::to_string(doc.code()).data(), doc.error().detail.c_str());
        return 2;
    }
    const auto& set = doc.value().samples;
    const auto integrity = validate_split_integrity(set);
    const auto tune = evaluate(set, Split::Tune);
    const auto validation = evaluate(set, Split::Validation);
    const auto holdout = evaluate(set, Split::Holdout);
    std::printf("dataset samples=%zu format=%s\n", set.size(),
                doc.value().schema_version >= 2 ? "diagcal_v2" : "diagcal_v1");
    print_stats("tune", tune);
    print_stats("validation", validation);
    print_stats("holdout", holdout);
    print_confusion("holdout", holdout);
    for (const auto& m : evaluate_models(set, Split::Holdout)) print_model(m);
    std::printf("integrity ok=%d issues=%zu notes=%zu\n", integrity.ok ? 1 : 0, integrity.issues.size(),
                integrity.notes.size());
    for (const auto& is : integrity.issues) std::printf("integrity issue=%s id=%s\n", is.code.c_str(), is.id.c_str());
    // EF-X20 dogrulama bulgusu 4: engellemeyen not (ornek kapiya zaten uygun degil; cikis kodunu 5 yapmaz).
    for (const auto& n : integrity.notes) std::printf("integrity note=%s id=%s\n", n.code.c_str(), n.id.c_str());
    const auto status = release_status(holdout, integrity);
    const std::size_t eligible = eligible_real(holdout);
    std::printf("gate ready=%d reason=%s real_holdout=%zu holdout_total=%zu synthetic_only=%d\n",
                status.ready ? 1 : 0, status.reason_key.c_str(), status.real_holdout, status.holdout_total,
                status.synthetic_only ? 1 : 0);
    std::printf("holdout eligible_real=%zu machines=%zu positives=%zu negatives=%zu covered_families=0x%08x\n", eligible,
                holdout.distinct_machines, holdout.positives, holdout.negatives,
                static_cast<unsigned>(holdout.covered_families));
    // EF-X20 dogrulama bulgusu 1/3: kapi oranlari yalniz uygun gercek orneklerden; ozetsiz ornek uygun degil.
    std::printf("holdout eligible_fp=%zu eligible_correct=%zu eligible_abstain=%zu ineligible_real=%zu "
                "missing_digest=%zu\n",
                holdout.eligible_false_positive, holdout.eligible_correct, holdout.eligible_abstained,
                holdout.real_ineligible, holdout.real_missing_digest);
    if (emit_header) {
        if (!status.ready) {
            std::fprintf(stderr, "error: refusing --emit-header: gate not ready (need real holdout and low FP rate)\n");
            return 4;
        }
        const std::string hash = ef::core::recovery::sha256_hex(text);
        std::printf("--- validated_calibration.h ---\n");
        std::printf("#pragma once\n");
        std::printf("// DIAG-06: ef_diag_calibrate ile uretildi; elle duzenlemeyin.\n");
        std::printf("// dataset_sha256=%s collected_on=%s\n", hash.c_str(), collected_on.c_str());
        std::printf("#include \"core/diagnosis/release_gate.h\"\n\n");
        std::printf("namespace ef::core::diagnosis {\n");
        std::printf("inline constexpr ValidatedCalibration kValidatedCalibration{true, %zu, %zu, %zu, %zu, %zu, "
                    "\"%s\", \"%s\", %zu, %zu, %zu, 0x%08xu};\n",
                    // Bulgu 1: total/false_positive/abstained/correct uygun gercek kumeden (sentetik karisik degil).
                    eligible, eligible, holdout.eligible_false_positive, holdout.eligible_abstained,
                    holdout.eligible_correct,
                    hash.c_str(), collected_on.c_str(), holdout.distinct_machines, holdout.positives,
                    holdout.negatives, static_cast<unsigned>(holdout.covered_families));
        std::printf("}  // namespace ef::core::diagnosis\n");
    }
    if (!integrity.ok) return 5;  // sizinti/yinelenmis makine
    return status.ready ? 0 : 3;
}
