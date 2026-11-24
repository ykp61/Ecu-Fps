# Metin dosyalarini C++ bayt dizisine gomer (derleme zamani). Kullanim:
#   cmake -DOUT=<out.cpp> -DNAMESPACE=<ns> -DENTRIES="sym1=path1|sym2=path2" -P embed.cmake
# Her sembol icin: extern const std::string_view <sym>;
cmake_minimum_required(VERSION 3.25)

set(body "// Otomatik uretildi (cmake/embed.cmake). Elle duzenlemeyin.\n#include <string_view>\n\nnamespace ${NAMESPACE} {\n")
string(REPLACE "|" ";" entry_list "${ENTRIES}")
foreach(entry IN LISTS entry_list)
    string(REPLACE "=" ";" parts "${entry}")
    list(GET parts 0 sym)
    list(GET parts 1 path)
    file(READ "${path}" hex HEX)
    string(LENGTH "${hex}" hexlen)
    math(EXPR size "${hexlen} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND body "namespace { constexpr unsigned char ${sym}_data[] = {${bytes} 0x00}; }\n")
    string(APPEND body "extern const std::string_view ${sym}(reinterpret_cast<const char*>(${sym}_data), ${size});\n")
endforeach()
string(APPEND body "}  // namespace ${NAMESPACE}\n")
file(WRITE "${OUT}.tmp" "${body}")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
