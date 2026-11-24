#pragma once
// Sinirli, bagimliliksiz JSON okuyucu/yazici. Tum dis girdiler (journal, profil, dataset, IPC)
// boyut/derinlik/uzunluk/eleman/sayi sinirindan gecer. Hata istisna degil Result ile doner.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"

namespace ef::core::json {

struct Limits {
    std::size_t max_bytes = 1u << 20;   // 1 MiB
    int max_depth = 32;
    std::size_t max_string = 4096;      // bayt
    std::size_t max_items = 10000;      // toplam dizi/nesne elemani
};

class Value {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Value() = default;
    static Value null() { return Value(); }
    static Value boolean(bool b) { Value v; v.type_ = Type::Bool; v.b_ = b; return v; }
    static Value integer(std::int64_t i) { Value v; v.type_ = Type::Int; v.i_ = i; return v; }
    static Value number(double d) { Value v; v.type_ = Type::Double; v.d_ = d; return v; }
    static Value string(std::string s) { Value v; v.type_ = Type::String; v.s_ = std::move(s); return v; }
    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const noexcept { return type_; }
    bool is(Type t) const noexcept { return type_ == t; }

    bool as_bool() const noexcept { return b_; }
    std::int64_t as_int() const noexcept { return i_; }
    double as_double() const noexcept { return type_ == Type::Int ? static_cast<double>(i_) : d_; }
    const std::string& as_string() const noexcept { return s_; }

    // Dizi
    const std::vector<Value>& items() const noexcept { return items_; }
    void push(Value v) { items_.push_back(std::move(v)); }

    // Nesne (ekleme sirasi korunur; tekrarli anahtar okuyucu tarafindan reddedilir)
    const std::vector<std::string>& keys() const noexcept { return keys_; }
    const Value* find(std::string_view key) const noexcept;
    void set(std::string key, Value v);

private:
    Type type_ = Type::Null;
    bool b_ = false;
    std::int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::vector<Value> items_;      // dizi elemanlari veya nesne degerleri
    std::vector<std::string> keys_; // nesne anahtarlari (items_ ile ayni sira)
};

contracts::Result<Value> parse(std::string_view text, const Limits& limits = {});
std::string write(const Value& v);

// ---- Sema ve alan erisimi: hepsi sinir kontrollu ----

// root.schema_version zorunlu; > supported ise SchemaTooNew (eski kod yeni veriyi tahmin etmez).
contracts::Result<int> check_schema(const Value& root, int supported);

contracts::Result<std::string> get_string(const Value& obj, std::string_view key, std::size_t max_len);
contracts::Result<std::int64_t> get_int(const Value& obj, std::string_view key, std::int64_t min, std::int64_t max);
contracts::Result<bool> get_bool(const Value& obj, std::string_view key);

}  // namespace ef::core::json
