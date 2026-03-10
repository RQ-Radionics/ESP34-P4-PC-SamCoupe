// ESP32 stub for fmt/chrono.h
//
// fmt/chrono.h unconditionally #includes <locale>, which pulls in
// cxx11-locale-inst.o from libstdc++.a. That object has dozens of .sbss
// sections using GP-relative addressing, which the ESP-IDF linker cannot
// place with --enable-non-contiguous-regions.
//
// SimCoupe only uses fmt::localtime() + "{:%Y%m%d}" formatting in ATA.cpp,
// which is unreachable on ESP32 (IDEDisk is stubbed out). So we provide a
// minimal stub that satisfies the #include without pulling in locale.
//
// We define FMT_CHRONO_H_ to prevent the real fmt/chrono.h from being
// included if somehow included again via a different path.

#ifndef FMT_CHRONO_H_
#define FMT_CHRONO_H_

#include <chrono>
#include <ctime>
#include <fmt/format.h>

FMT_BEGIN_NAMESPACE

// Minimal localtime/gmtime wrappers — only needed by ATA.cpp which is
// unreachable on ESP32. Provided so the translation unit compiles.
inline std::tm localtime(std::time_t time) {
    std::tm tm_val{};
    localtime_r(&time, &tm_val);
    return tm_val;
}

inline std::tm gmtime(std::time_t time) {
    std::tm tm_val{};
    gmtime_r(&time, &tm_val);
    return tm_val;
}

// Stub formatter for std::tm with chrono format specs like "{:%Y%m%d}".
// This is only called from ATA.cpp which is unreachable on ESP32.
template <>
struct formatter<std::tm> {
    // Parse a strftime-style format spec (e.g. "%Y%m%d")
    char fmt_str[32] = "%Y%m%d";

    FMT_CONSTEXPR auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        auto it = ctx.begin();
        auto end = ctx.end();
        size_t i = 0;
        // Skip leading ':'
        if (it != end && *it == ':') ++it;
        while (it != end && *it != '}' && i < sizeof(fmt_str) - 1) {
            fmt_str[i++] = *it++;
        }
        fmt_str[i] = '\0';
        return it;
    }

    template <typename FormatContext>
    auto format(const std::tm& tm, FormatContext& ctx) const -> decltype(ctx.out()) {
        char buf[64];
        strftime(buf, sizeof(buf), fmt_str, &tm);
        return format_to(ctx.out(), "{}", buf);
    }
};

FMT_END_NAMESPACE

#endif  // FMT_CHRONO_H_
