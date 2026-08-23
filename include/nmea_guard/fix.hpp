// Folds GGA, RMC, GSA and VTG sentences into one fix per epoch.
#pragma once

#include <cstdint>
#include <optional>

#include "nmea_guard/sentences.hpp"

namespace nmea_guard {

struct Fix {
    std::optional<UtcTime> time;
    std::optional<UtcDate> date;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude_m;
    std::optional<double> speed_knots;
    std::optional<double> course_deg;
    std::optional<std::uint32_t> quality;     // GGA fix quality
    std::optional<std::uint32_t> satellites;  // GGA satellites in use
    std::optional<std::uint32_t> fix_type;    // GSA 1/2/3
    std::optional<double> hdop;
    std::optional<double> pdop;
    std::optional<double> vdop;
    std::optional<char> status;  // RMC 'A' / 'V'

    // A fix is usable when it has a position and the receiver itself says the
    // position is valid (RMC status 'A' or a non-zero GGA quality).
    bool valid() const noexcept {
        if (!latitude || !longitude) return false;
        if (status) return *status == 'A';
        return quality && *quality > 0;
    }
};

class FixAccumulator {
public:
    // Returns true when `s` started a new epoch, i.e. the previous fix is
    // complete and was moved to last_complete().
    bool update(const Sentence& s) noexcept;

    // Close the open epoch without waiting for the next timestamp. Returns
    // true if there was anything to close.
    bool close() noexcept;

    const Fix& current() const noexcept { return current_; }
    const Fix& last_complete() const noexcept { return complete_; }
    bool has_complete() const noexcept { return has_complete_; }

private:
    void start_epoch_if_new(const std::optional<UtcTime>& t, bool& rolled) noexcept;

    Fix current_;
    Fix complete_;
    bool has_complete_ = false;
    bool epoch_has_data_ = false;
};

}  // namespace nmea_guard
