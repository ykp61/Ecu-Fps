// Ecu Fps shim: ReportException gerceklemesi (yalniz mesaj metni).
#include "Exception.h"

namespace pmon::util
{
    std::string ReportException(std::string note, std::exception_ptr pEx) noexcept
    {
        try {
            if (!pEx) {
                pEx = std::current_exception();
            }
            std::string text = std::move(note);
            if (pEx) {
                try {
                    std::rethrow_exception(pEx);
                }
                catch (const std::exception& e) {
                    if (!text.empty()) {
                        text += ": ";
                    }
                    text += e.what();
                }
                catch (...) {
                    if (!text.empty()) {
                        text += ": ";
                    }
                    text += "unknown exception";
                }
            }
            return text;
        }
        catch (...) {
            return {};
        }
    }
}
