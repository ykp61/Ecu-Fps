#pragma once
// Denetim 4.1: uygulamanin tum arka plan is parcaciklari buradan baslar.
//  - Is parcacigina ozgu std::terminate isleyicisi kurulur (MSVC'de terminate isleyicisi is parcacigi basinadir).
//  - Govdeden kacan C++ istisnasi yakalanir ve metniyle birlikte fatal_worker_exception'a gider: minidump yazilir,
//    surec sonlanir. Sessiz abort (dump'siz, nedensiz) yerine incelenebilir kayit; yarim kalan yazma islemi journal ile
//    bir sonraki acilista uzlastirilir. Belirsiz durumda UI'yi "suruyor" halinde birakip devam etmekten guvenlidir.
#include <exception>
#include <thread>
#include <utility>

#include "platform/windows/crash_handler.h"

namespace ef::app {

template <class F>
std::thread spawn_worker(F&& body) {
    return std::thread([fn = std::forward<F>(body)]() mutable {
        platform::guard_current_thread();
        try {
            fn();
        } catch (const std::exception& e) {
            platform::fatal_worker_exception(e.what());
        } catch (...) {
            platform::fatal_worker_exception("non-standard exception");
        }
    });
}

}  // namespace ef::app
