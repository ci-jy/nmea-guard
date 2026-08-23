// Baseline for comparison: the line-based parsing found in many hobby and
// vendor example sketches. It reads up to the newline, verifies the checksum
// only when "*hh" sits exactly at the end of the line, and otherwise trusts
// the sentence. Fields are not validated beyond the sentence type.
//
// This is deliberately not part of the library; nmea-replay uses it with
// --parser naive to show what slips through without strict framing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nmea_guard::baseline {

class NaiveParser {
public:
    static constexpr std::size_t kCapacity = 120;

    // Returns true when a line was accepted; line() then holds it.
    bool feed(std::uint8_t c) noexcept {
        if (c == '$') {
            len_ = 0;
        }
        if (c == '\n' || c == '\r') {
            const bool ok = len_ > 0 && accept();
            if (ok) out_len_ = len_;
            len_ = 0;
            return ok;
        }
        if (len_ < kCapacity) line_[len_++] = static_cast<char>(c);
        return false;
    }

    std::string_view line() const noexcept { return {line_, out_len_}; }

private:
    static int hex(char c) noexcept {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool accept() noexcept {
        if (line_[0] != '$' || len_ < 7) return false;
        const std::string_view type(line_ + 3, 3);
        if (type != "GGA" && type != "RMC" && type != "GSA" && type != "VTG") return false;
        if (len_ >= 4 && line_[len_ - 3] == '*') {
            std::uint8_t sum = 0;
            for (std::size_t i = 1; i < len_ - 3; ++i) sum ^= static_cast<std::uint8_t>(line_[i]);
            const int hi = hex(line_[len_ - 2]), lo = hex(line_[len_ - 1]);
            return hi >= 0 && lo >= 0 && sum == ((hi << 4) | lo);
        }
        // No checksum where we expected one: assume the receiver omitted it.
        return true;
    }

    char line_[kCapacity] = {};
    std::size_t len_ = 0;
    std::size_t out_len_ = 0;
};

}  // namespace nmea_guard::baseline
