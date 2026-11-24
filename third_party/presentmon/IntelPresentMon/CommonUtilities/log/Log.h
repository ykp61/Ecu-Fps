// Ecu Fps shim: Intel PresentMon CommonUtilities/log/Log.h yerine.
// Arayuz Intel PresentMon v2.6.0'dan turetilmistir (Copyright (C) 2022 Intel Corporation, MIT).
// Gunluk sistemi tasinmaz: tum pmlog_* cagrilari derlenir ama hicbir sey yazmaz.
// Arguman ifadeleri (std::format, string birlestirme) tip denetiminden gecer;
// GetLogLevel() sabit None oldugu icin gunluk dali hic calismaz.
#pragma once
#include <cstdint>

namespace pmon::util::log
{
    enum class Level : uint16_t
    {
        None = 0,
        Fatal = 10,
        Error = 20,
        Warning = 30,
        Info = 40,
        Performance = 50,
        Debug = 60,
        Verbose = 70,
        Verbose2 = 80,
        EndOfEnumKeys,
    };

    class GlobalPolicy
    {
    public:
        static const GlobalPolicy& Get() noexcept
        {
            static const GlobalPolicy instance{};
            return instance;
        }
        constexpr Level GetLogLevel() const noexcept { return Level::None; }
    };

    // Upstream EntryBuilder'in PresentData'nin kullandigi zincir metotlari; hepsi bos.
    struct EntryBuilder
    {
        template<class... A> EntryBuilder& note(A&&...) noexcept { return *this; }
        template<class... A> EntryBuilder& watch(A&&...) noexcept { return *this; }
        template<class... A> EntryBuilder& hr(A&&...) noexcept { return *this; }
        template<class... A> EntryBuilder& code(A&&...) noexcept { return *this; }
        template<class... A> EntryBuilder& first(A&&...) noexcept { return *this; }
        template<class... A> EntryBuilder& every(A&&...) noexcept { return *this; }
        EntryBuilder& diag() noexcept { return *this; }
    };

    struct Voidifier_ { template<class T> void operator&(T&&) const {} };
}

#define pmlog_(lvl) (::pmon::util::log::GlobalPolicy::Get().GetLogLevel() < lvl) \
    ? (void)0 : ::pmon::util::log::Voidifier_{} & ::pmon::util::log::EntryBuilder{}
#define pmlog_fatal pmlog_(::pmon::util::log::Level::Fatal).note
#define pmlog_error pmlog_(::pmon::util::log::Level::Error).note
#define pmlog_warn  pmlog_(::pmon::util::log::Level::Warning).note
#define pmlog_info  pmlog_(::pmon::util::log::Level::Info).note
#define pmlog_dbg   pmlog_(::pmon::util::log::Level::Debug).note

#define pmwatch(expr) watch(#expr, (expr))
