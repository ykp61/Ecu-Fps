#pragma once
// SettingOperation <-> JSON. Dis veri yalniz derlenmis OperationId/OperationScope kumesine eslenir;
// hedef alani dosya yolu, komut veya kabuk karakteri tasiyamaz.

#include <string_view>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/serialization/json.h"

namespace ef::core::json {

inline constexpr std::size_t kMaxTargetLen = 640;  // hex kodlu baslangic adlari (255 bayt) dahil
inline constexpr std::size_t kMaxValueLen = 256;

// Izinli karakterler: harf, rakam, _ . : - { } (GUID ve typed anahtarlar icin).
// "..", yol ayiricilari, bosluk, tirnak ve kabuk karakterleri reddedilir.
bool is_safe_target(std::string_view s) noexcept;

contracts::Result<contracts::SettingOperation> decode_operation(const Value& v);
Value encode_operation(const contracts::SettingOperation& op);

}  // namespace ef::core::json
