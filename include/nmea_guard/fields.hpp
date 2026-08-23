// Field-level helpers: splitting a sentence body on commas and strict,
// allocation-free conversion of individual fields.
//
// Every parser returns false when the field is present but malformed, and
// leaves the output empty when the field is simply blank. That difference is
// what lets the decoder tell "receiver has no value yet" from "bytes were lost".
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace nmea_guard {

// Up to kMaxFields views into a sentence body. Field 0 is the address
// ("GPGGA"). Views point into the caller's buffer.
class FieldList {
public:
    static constexpr std::size_t kMaxFields = 40;

    // Returns false if the body has more than kMaxFields fields.
    bool split(std::string_view body) noexcept;

    std::size_t size() const noexcept { return count_; }
    std::string_view operator[](std::size_t i) const noexcept {
        return i < count_ ? fields_[i] : std::string_view{};
    }

private:
    std::string_view fields_[kMaxFields];
    std::size_t count_ = 0;
};

struct UtcTime {
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;
    std::uint32_t microsecond = 0;

    friend bool operator==(const UtcTime& a, const UtcTime& b) noexcept {
        return a.hour == b.hour && a.minute == b.minute && a.second == b.second &&
               a.microsecond == b.microsecond;
    }
    friend bool operator!=(const UtcTime& a, const UtcTime& b) noexcept { return !(a == b); }
};

struct UtcDate {
    std::uint8_t day = 0;
    std::uint8_t month = 0;
    std::uint8_t year = 0;  // two digits, as transmitted
};

namespace field {

// [-]digits[.digits] or [-].digits. No exponent, no '+', no spaces.
bool parse_decimal(std::string_view s, std::optional<double>& out) noexcept;
// Plain unsigned integer.
bool parse_uint(std::string_view s, std::optional<std::uint32_t>& out) noexcept;
// hhmmss[.s...]
bool parse_time(std::string_view s, std::optional<UtcTime>& out) noexcept;
// ddmmyy
bool parse_date(std::string_view s, std::optional<UtcDate>& out) noexcept;
// ddmm.mmm with hemisphere N/S, or dddmm.mmm with E/W. Value and hemisphere
// must both be present or both be blank. Output is signed decimal degrees.
bool parse_latitude(std::string_view value, std::string_view hemi, std::optional<double>& out) noexcept;
bool parse_longitude(std::string_view value, std::string_view hemi, std::optional<double>& out) noexcept;
// A single character from `allowed`, or blank.
bool parse_flag(std::string_view s, std::string_view allowed, std::optional<char>& out) noexcept;

}  // namespace field
}  // namespace nmea_guard
