#pragma once
// Hafif Result<T>: istisna yerine acik hata donusu. Hata kodlari typed.

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ef::contracts {

enum class ErrorCode {
    InvalidEnum,      // bilinmeyen enum degeri
    UnitMismatch,     // farkli birimler karistirildi
    MissingValue,     // deger yok (eksik olcum)
    OutOfRange,       // sinir disi sayi
    InvalidTransition,// gecersiz yasam dongusu gecisi
    SchemaTooNew,     // bilinmeyen/yeni sema surumu
    ParseLimit,       // boyut/derinlik/uzunluk siniri
    ParseError,       // bicim hatasi
    NegativeDuration, // monoton zamanda geriye gidis
    NotAligned,       // zaman kaynaklari eslenemedi
    IoError,
    Unsupported,
    Denied,
    GateBlocked,      // oyun/algilama kapisi islem sinirinda reddetti; ayrinti gate.* anahtari
};

std::string_view to_string(ErrorCode c) noexcept;

struct Error {
    ErrorCode code;
    std::string detail;
};

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : v_(std::move(value)) {}           // NOLINT(google-explicit-constructor)
    Result(Error error) : v_(std::move(error)) {}       // NOLINT(google-explicit-constructor)

    static Result fail(ErrorCode c, std::string detail = {}) { return Result(Error{c, std::move(detail)}); }

    bool ok() const noexcept { return std::holds_alternative<T>(v_); }
    explicit operator bool() const noexcept { return ok(); }

    const T& value() const& { return std::get<T>(v_); }
    T& value() & { return std::get<T>(v_); }
    T&& value() && { return std::get<T>(std::move(v_)); }
    T value_or(T fallback) const& { return ok() ? std::get<T>(v_) : fallback; }
    const Error& error() const& { return std::get<Error>(v_); }
    ErrorCode code() const { return std::get<Error>(v_).code; }

private:
    std::variant<T, Error> v_;
};

struct Ok {};
using Status = Result<Ok>;
inline Status ok_status() { return Status(Ok{}); }

}  // namespace ef::contracts
