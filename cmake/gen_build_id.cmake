# EF-X18 dogrulama bulgusu 1: derleme kimligi HER derlemede uretilir (yalniz configure aninda degil).
# Kullanim: cmake -DEF_GIT=<git yolu veya bos> -DEF_SRC=<kaynak kok> -DEF_OUT=<uretilen baslik> -P gen_build_id.cmake
# Kurallar:
#  - Kimlik = `git rev-parse --short=12 HEAD`. Git yoksa/basarisizsa "unknown" (gate kapsami olusmaz).
#  - Izlenen dosyalardaki her degisiklik VEYA derlemeye giren klasorlerdeki (src, cmake, third_party, design,
#    kok CMakeLists.txt) izlenmeyen dosya "-dirty" ekler. Git status hatasi da "-dirty" sayilir (fail-closed).
#  - Worktree'de .git bir dosyadir; git komutlari EF_SRC calisma dizininde calistigi icin bunu kendisi cozer.
#  - Baslik yalniz icerik degisince yeniden yazilir: kimlik ayniysa bagimli kaynaklar yeniden derlenmez.
set(id "unknown")
if(EF_GIT)
    execute_process(COMMAND "${EF_GIT}" rev-parse --short=12 HEAD WORKING_DIRECTORY "${EF_SRC}"
                    OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
    if(rc EQUAL 0 AND head MATCHES "^[0-9a-f]+$")
        set(id "${head}")
        execute_process(COMMAND "${EF_GIT}" --no-optional-locks status --porcelain --untracked-files=no
                        WORKING_DIRECTORY "${EF_SRC}" OUTPUT_VARIABLE tracked ERROR_QUIET RESULT_VARIABLE rc_t)
        execute_process(COMMAND "${EF_GIT}" --no-optional-locks status --porcelain --untracked-files=all
                                -- src cmake third_party design CMakeLists.txt
                        WORKING_DIRECTORY "${EF_SRC}" OUTPUT_VARIABLE untracked ERROR_QUIET RESULT_VARIABLE rc_u)
        if(NOT rc_t EQUAL 0 OR NOT rc_u EQUAL 0 OR NOT tracked STREQUAL "" OR NOT untracked STREQUAL "")
            string(APPEND id "-dirty")
        endif()
    endif()
endif()
set(content "#pragma once\n// Uretilen dosya (cmake/gen_build_id.cmake); elle duzenlemeyin.\n#define EF_BUILD_ID_STR \"${id}\"\n")
set(old "")
if(EXISTS "${EF_OUT}")
    file(READ "${EF_OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${EF_OUT}" "${content}")
endif()
if(EF_PRINT)
    message(STATUS "Ecu Fps derleme kimligi: ${id}")
endif()
