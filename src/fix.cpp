#include "nmea_guard/fix.hpp"

namespace nmea_guard {

namespace {

template <typename T>
void take(std::optional<T>& dst, const std::optional<T>& src) noexcept {
    if (src) dst = src;
}

}  // namespace

void FixAccumulator::start_epoch_if_new(const std::optional<UtcTime>& t, bool& rolled) noexcept {
    // GGA and RMC carry the epoch time; GSA and VTG do not and simply join
    // whatever epoch is open. A new timestamp closes the previous epoch.
    if (!t) return;
    if (current_.time && *current_.time == *t) return;
    if (epoch_has_data_) {
        complete_ = current_;
        has_complete_ = true;
        rolled = true;
    }
    // Keep the date across epochs: only RMC carries it, and some receivers
    // send RMC less often than GGA.
    const std::optional<UtcDate> date = current_.date;
    current_ = Fix{};
    current_.date = date;
    current_.time = t;
    epoch_has_data_ = false;
}

bool FixAccumulator::close() noexcept {
    if (!epoch_has_data_) return false;
    complete_ = current_;
    has_complete_ = true;
    epoch_has_data_ = false;
    return true;
}

bool FixAccumulator::update(const Sentence& s) noexcept {
    bool rolled = false;
    switch (s.type) {
        case SentenceType::kGGA: {
            const Gga& g = s.gga;
            start_epoch_if_new(g.time, rolled);
            take(current_.latitude, g.latitude);
            take(current_.longitude, g.longitude);
            take(current_.altitude_m, g.altitude_m);
            take(current_.quality, g.quality);
            take(current_.satellites, g.satellites);
            take(current_.hdop, g.hdop);
            break;
        }
        case SentenceType::kRMC: {
            const Rmc& r = s.rmc;
            start_epoch_if_new(r.time, rolled);
            take(current_.date, r.date);
            take(current_.latitude, r.latitude);
            take(current_.longitude, r.longitude);
            take(current_.speed_knots, r.speed_knots);
            take(current_.course_deg, r.course_deg);
            take(current_.status, r.status);
            break;
        }
        case SentenceType::kGSA: {
            const Gsa& g = s.gsa;
            take(current_.fix_type, g.fix_type);
            take(current_.pdop, g.pdop);
            take(current_.hdop, g.hdop);
            take(current_.vdop, g.vdop);
            break;
        }
        case SentenceType::kVTG: {
            const Vtg& v = s.vtg;
            take(current_.speed_knots, v.speed_knots);
            take(current_.course_deg, v.course_true_deg);
            break;
        }
        case SentenceType::kUnknown:
            return false;
    }
    epoch_has_data_ = true;
    return rolled;
}

}  // namespace nmea_guard
