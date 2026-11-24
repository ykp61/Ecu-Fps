#include <cstdint>
#include <string>

#include "core/serialization/json.h"
#include "core/serialization/operation_codec.h"
#include "ef_test.h"

using namespace ef::core::json;
using ef::contracts::ErrorCode;

EF_TEST(json_parses_basic_document) {
    auto r = parse(R"({"schema_version":1,"name":"İstanbul \u00e7","n":-42,"f":1.5,"ok":true,"z":null,"a":[1,2,3]})");
    EF_CHECK(r.ok());
    const auto& v = r.value();
    EF_CHECK(get_int(v, "n", -100, 100).value() == -42);
    EF_CHECK(get_string(v, "name", 64).value() == "İstanbul ç");
    EF_CHECK(v.find("f")->as_double() == 1.5);
    EF_CHECK(get_bool(v, "ok").value());
    EF_CHECK(v.find("z")->is(Value::Type::Null));
    EF_CHECK(v.find("a")->items().size() == 3);
}

EF_TEST(json_round_trip) {
    auto r = parse(R"({"s":"a\"b\\c\n\u0001","i":9007199254740993,"arr":[{"x":[]},{}]})");
    EF_CHECK(r.ok());
    const std::string out = write(r.value());
    auto r2 = parse(out);
    EF_CHECK(r2.ok());
    EF_CHECK(write(r2.value()) == out);
    EF_CHECK(get_int(r2.value(), "i", 0, INT64_MAX).value() == 9007199254740993LL);
}

EF_TEST(json_truncated_and_malformed_rejected) {
    for (const char* bad : {"", "{", "{\"a\":", "{\"a\":1", "[1,2", "\"abc", "{\"a\" 1}", "[1,]", "{,}", "01",
                            "1 2", "tru", "{\"a\":1}x", "[\"\\x\"]", "-", "1.", "{\"a\":1,\"a\":2}"}) {
        auto r = parse(bad);
        EF_CHECK(!r.ok());
    }
}

EF_TEST(json_limits_enforced) {
    Limits l;
    l.max_depth = 8;
    std::string deep(9 + 1, '[');
    deep += std::string(10, ']');
    auto d = parse(deep, l);
    EF_CHECK(!d.ok());
    EF_CHECK(d.code() == ErrorCode::ParseLimit);

    std::string very_deep(100000, '[');  // yigin tasmasi olmadan reddedilmeli
    EF_CHECK(parse(very_deep).code() == ErrorCode::ParseLimit);

    Limits ls;
    ls.max_string = 16;
    EF_CHECK(parse("\"" + std::string(17, 'a') + "\"", ls).code() == ErrorCode::ParseLimit);

    Limits lb;
    lb.max_bytes = 100;
    EF_CHECK(parse(std::string(101, ' '), lb).code() == ErrorCode::ParseLimit);

    Limits li;
    li.max_items = 5;
    EF_CHECK(parse("[1,2,3,4,5,6]", li).code() == ErrorCode::ParseLimit);
}

EF_TEST(json_number_overflow_and_range) {
    EF_CHECK(parse("99999999999999999999").code() == ErrorCode::OutOfRange);
    EF_CHECK(parse("1e999").code() == ErrorCode::OutOfRange);
    auto r = parse(R"({"v":500})");
    EF_CHECK(get_int(r.value(), "v", 0, 100).code() == ErrorCode::OutOfRange);
    EF_CHECK(get_int(r.value(), "missing", 0, 100).code() == ErrorCode::MissingValue);
    EF_CHECK(get_string(r.value(), "v", 10).code() == ErrorCode::ParseError);  // tur uyusmazligi
}

EF_TEST(json_utf8_validation) {
    EF_CHECK(parse("\xEF\xBB\xBF{\"a\":1}").ok());           // BOM atlanir
    EF_CHECK(!parse("\"\xC3\"").ok());                         // kesik UTF-8
    EF_CHECK(!parse("\"\xC0\xAF\"").ok());                     // asiri uzun kodlama
    EF_CHECK(!parse("\"\xED\xA0\x80\"").ok());                 // vekil kod noktasi
    EF_CHECK(!parse("\"\xFF\"").ok());
    EF_CHECK(!parse("\"\\ud800\"").ok());                      // yalniz yuksek vekil
    EF_CHECK(!parse("\"\\u0000\"").ok());                      // NUL
    EF_CHECK(parse("\"\\ud83d\\ude00\"").ok());                // gecerli cift
    EF_CHECK(!parse("\"a\tb\"").ok());                         // kacissiz kontrol karakteri
}

EF_TEST(json_schema_version) {
    EF_CHECK(check_schema(parse(R"({"schema_version":1})").value(), 1).value() == 1);
    EF_CHECK(check_schema(parse(R"({"schema_version":2})").value(), 1).code() == ErrorCode::SchemaTooNew);
    EF_CHECK(check_schema(parse(R"({"schema_version":0})").value(), 1).code() == ErrorCode::OutOfRange);
    EF_CHECK(check_schema(parse(R"({"schema_version":"1"})").value(), 1).code() == ErrorCode::ParseError);
    EF_CHECK(check_schema(parse(R"({})").value(), 1).code() == ErrorCode::MissingValue);
    EF_CHECK(check_schema(parse(R"([1])").value(), 1).code() == ErrorCode::ParseError);
}

EF_TEST(operation_decode_accepts_only_compiled_ops) {
    auto ok = parse(R"({"op":"power_mode_set","scope":"machine","target":"overlay:{961cc777-2547-4f9d-8174-7d86181b8a7a}","value":"best_performance"})");
    auto op = decode_operation(ok.value());
    EF_CHECK(op.ok());
    EF_CHECK(op.value().id == ef::contracts::OperationId::PowerModeSet);
    // encode -> decode turu
    auto again = decode_operation(encode_operation(op.value()));
    EF_CHECK(again.ok());
    EF_CHECK(again.value().target == op.value().target);

    auto shell = parse(R"({"op":"run_command","scope":"machine","target":"x","value":"cmd.exe /c del"})");
    EF_CHECK(decode_operation(shell.value()).code() == ErrorCode::InvalidEnum);
    auto extra = parse(R"({"op":"power_mode_set","scope":"machine","target":"x","value":"y","exec":"calc"})");
    EF_CHECK(decode_operation(extra.value()).code() == ErrorCode::ParseError);
}

EF_TEST(operation_target_cannot_be_path_or_command) {
    for (const char* t : {"C:\\Windows\\System32", "C:", "..\\..\\x", "a/b", "a b", "x;rm", "x|y", "\"q\"", "%TEMP%",
                          "HKLM\\Software", "\\\\server\\share", ""}) {
        EF_CHECK(!is_safe_target(t));
    }
    EF_CHECK(is_safe_target("scheme:{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}"));
    EF_CHECK(is_safe_target("run.hkcu.OneDrive"));
    EF_CHECK(!is_safe_target(std::string(641, 'a')));
}

EF_TEST(json_fuzz_mutations_never_crash) {
    const std::string seed = R"({"schema_version":1,"ops":[{"op":"power_mode_set","scope":"machine","target":"a","value":"b"}],"x":[1.5,-2,true,null,"\u00e7"]})";
    std::uint32_t rng = 12345;
    auto next = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng; };
    int parsed = 0;
    for (int i = 0; i < 20000; ++i) {
        std::string s = seed;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits; ++e) {
            const std::size_t pos = next() % s.size();
            switch (next() % 3) {
                case 0: s[pos] = static_cast<char>(next() & 0xFF); break;
                case 1: s.erase(pos, 1); break;
                default: s.insert(pos, 1, static_cast<char>(next() & 0xFF)); break;
            }
            if (s.empty()) s = "x";
        }
        auto r = parse(s);  // cokme yok; ya basarili ya kontrollu hata
        if (r.ok()) {
            ++parsed;
            (void)write(r.value());
            if (auto* ops = r.value().find("ops"); ops && ops->is(Value::Type::Array))
                for (const auto& o : ops->items()) (void)decode_operation(o);
        } else {
            EF_CHECK(!r.error().detail.empty());
        }
    }
    EF_CHECK(parsed < 20000);
}
