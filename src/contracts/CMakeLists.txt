# Tipli veri sozlesmeleri (docs/16). Win32 cagrisi yok; saf C++20.
add_library(ef_contracts STATIC
    contracts.cpp
    metrics.cpp
    inventory.cpp)
target_include_directories(ef_contracts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ef_contracts PUBLIC ef_options)
