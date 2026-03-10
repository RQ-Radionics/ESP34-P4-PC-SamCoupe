idf_component_register(
    SRCS "main.cpp" "fatfs_alloc.c"
    INCLUDE_DIRS "."
    LDFRAGMENTS "simcoupe.lf"
    REQUIRES
        nvs_flash
        esp_timer
        freertos
        sim_audio
        sim_kbd
        sim_display
        sim_sdcard
        simcoupe_core
)

# main.cpp includes SimCoupe.h which pulls in ghc::filesystem.
# Apply the same defines as simcoupe_core so ghc picks the POSIX path.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    GHC_OS_LINUX
    GHC_OS_DETECTED
    "lstat=stat"
)

# Use C++17 (same as simcoupe_core) to avoid C++23 chrono_io.h pulling in
# <istream> which triggers locale_init.o in the link.
# Do NOT add -fexceptions here: the global sdkconfig disables exceptions to
# avoid libstdc++ .sbss.* sections that --enable-non-contiguous-regions
# cannot place. simcoupe_core handles -fexceptions at component level only.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)

