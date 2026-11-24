// Ecu Fps shim: Intel PresentMon CommonUtilities/Exception.h yerine.
// Arayuz Intel PresentMon v2.6.0'dan turetilmistir (Copyright (C) 2022 Intel Corporation, MIT).
// Yigin izi, PM_STATUS ve PresentMonAPI2 bagimliligi yoktur.
#pragma once
#include "str/String.h"
#include <exception>
#include <string>
#include <utility>

namespace pmon::util
{
    class Exception : public std::exception
    {
    public:
        Exception() noexcept = default;
        Exception(std::string msg) noexcept : note_{ std::move(msg) } {}
        const char* what() const noexcept override { return note_.c_str(); }
        const std::string& GetNote() const { return note_; }
    private:
        std::string note_;
    };

    template<class E = Exception, typename... R>
    auto Except(R&&... args)
    {
        E exception{ std::forward<R>(args)... };
        return exception;
    }

    // Upstream (mesaj, PM_STATUS) cifti doner; burada yalniz mesaj uretilir.
    std::string ReportException(std::string note = {}, std::exception_ptr pEx = {}) noexcept;

#define PM_DEFINE_EX_FROM(base, name) class name : public base { public: using base::base; }
#define PM_DEFINE_EX(name) class name : public ::pmon::util::Exception { public: using Exception::Exception; }

#define pmquell(stat) try { stat; } catch (...) {}
#define pmcatch_report catch (...) { pmlog_error(::pmon::util::ReportException()); }
}
