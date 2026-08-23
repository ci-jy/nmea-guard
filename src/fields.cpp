#include "nmea_guard/fields.hpp"

namespace nmea_guard {

bool FieldList::split(std::string_view body) noexcept {
    count_ = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == ',') {
            if (count_ >= kMaxFields) return false;
            fields_[count_++] = body.substr(start, i - start);
            start = i + 1;
        }
    }
    return true;
}

namespace field {
namespace {

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool all_digits(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (char c : s)
        if (!is_digit(c)) return false;
    return true;
}

unsigned two_digits(std::string_view s, std::size_t at) noexcept {
    return static_cast<unsigned>((s[at] - '0') * 10 + (s[at + 1] - '0'));
}

// Shared by latitude and longitude: `deg_digits` digits of whole degrees,
// two of whole minutes, then an optional fraction of a minute.
bool parse_angle(std::string_view value, std::string_view hemi, std::size_t deg_digits,
                 double max_deg, char pos, char neg, std::optional<double>& out) noexcept {
    out.reset();
    if (value.empty() && hemi.empty()) return true;
    if (value.empty() || hemi.size() != 1) return false;
    if (hemi[0] != pos && hemi[0] != neg) return false;

    const std::size_t dot = value.find('.');
    const std::size_t whole = dot == std::string_view::npos ? value.size() : dot;
    if (whole != deg_digits + 2) return false;
    if (!all_digits(value.substr(0, whole))) return false;
    if (dot != std::string_view::npos && !all_digits(value.substr(dot + 1))) return false;

    unsigned deg = 0;
    for (std::size_t i = 0; i < deg_digits; ++i) deg = deg * 10 + static_cast<unsigned>(value[i] - '0');
    std::optional<double> minutes;
    if (!parse_decimal(value.substr(deg_digits), minutes) || !minutes) return false;
    if (*minutes >= 60.0) return false;

    const double degrees = deg + *minutes / 60.0;
    if (degrees > max_deg) return false;
    out = hemi[0] == neg ? -degrees : degrees;
    return true;
}

}  // namespace

bool parse_decimal(std::string_view s, std::optional<double>& out) noexcept {
    out.reset();
    if (s.empty()) return true;
    std::size_t i = 0;
    bool negative = false;
    if (s[0] == '-') {
        negative = true;
        i = 1;
    }
    // Integer accumulation keeps the result exact for the digit counts NMEA
    // uses, and avoids strtod (locale, errno, and a lot of code on an MCU).
    std::uint64_t mantissa = 0;
    int digits = 0;
    int frac_digits = 0;
    bool seen_dot = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') {
            if (seen_dot) return false;
            seen_dot = true;
            continue;
        }
        if (!is_digit(c)) return false;
        if (digits >= 18) return false;
        mantissa = mantissa * 10 + static_cast<unsigned>(c - '0');
        ++digits;
        if (seen_dot) ++frac_digits;
    }
    if (digits == 0) return false;
    double v = static_cast<double>(mantissa);
    // Dividing by an exact power of ten rounds once, which matches Python's float().
    double scale = 1.0;
    for (int k = 0; k < frac_digits; ++k) scale *= 10.0;
    v /= scale;
    out = negative ? -v : v;
    return true;
}

bool parse_uint(std::string_view s, std::optional<std::uint32_t>& out) noexcept {
    out.reset();
    if (s.empty()) return true;
    if (!all_digits(s) || s.size() > 9) return false;
    std::uint32_t v = 0;
    for (char c : s) v = v * 10 + static_cast<std::uint32_t>(c - '0');
    out = v;
    return true;
}

bool parse_time(std::string_view s, std::optional<UtcTime>& out) noexcept {
    out.reset();
    if (s.empty()) return true;
    if (s.size() < 6 || !all_digits(s.substr(0, 6))) return false;
    UtcTime t;
    const unsigned h = two_digits(s, 0), m = two_digits(s, 2), sec = two_digits(s, 4);
    if (h > 23 || m > 59 || sec > 60) return false;  // 60 allows a leap second
    t.hour = static_cast<std::uint8_t>(h);
    t.minute = static_cast<std::uint8_t>(m);
    t.second = static_cast<std::uint8_t>(sec);
    if (s.size() > 6) {
        if (s[6] != '.') return false;
        const std::string_view frac = s.substr(7);
        if (!frac.empty() && !all_digits(frac)) return false;
        std::uint32_t us = 0;
        for (std::size_t i = 0; i < 6; ++i)
            us = us * 10 + (i < frac.size() ? static_cast<std::uint32_t>(frac[i] - '0') : 0);
        t.microsecond = us;
    }
    out = t;
    return true;
}

bool parse_date(std::string_view s, std::optional<UtcDate>& out) noexcept {
    out.reset();
    if (s.empty()) return true;
    if (s.size() != 6 || !all_digits(s)) return false;
    UtcDate d;
    const unsigned day = two_digits(s, 0), month = two_digits(s, 2);
    if (day < 1 || day > 31 || month < 1 || month > 12) return false;
    d.day = static_cast<std::uint8_t>(day);
    d.month = static_cast<std::uint8_t>(month);
    d.year = static_cast<std::uint8_t>(two_digits(s, 4));
    out = d;
    return true;
}

bool parse_latitude(std::string_view value, std::string_view hemi, std::optional<double>& out) noexcept {
    return parse_angle(value, hemi, 2, 90.0, 'N', 'S', out);
}

bool parse_longitude(std::string_view value, std::string_view hemi, std::optional<double>& out) noexcept {
    return parse_angle(value, hemi, 3, 180.0, 'E', 'W', out);
}

bool parse_flag(std::string_view s, std::string_view allowed, std::optional<char>& out) noexcept {
    out.reset();
    if (s.empty()) return true;
    if (s.size() != 1 || allowed.find(s[0]) == std::string_view::npos) return false;
    out = s[0];
    return true;
}

}  // namespace field
}  // namespace nmea_guard
