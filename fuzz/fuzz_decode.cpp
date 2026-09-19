// libFuzzer target: arbitrary text straight into the field decoder.
//
// Through the Parser, the fuzzer has to get the XOR checksum right before
// any field is looked at. This target skips framing so the field parsers
// (times, dates, angles, decimals, PRN lists) get the coverage.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "nmea_guard/sentences.hpp"

namespace ng = nmea_guard;

namespace {

void check(bool ok) {
    if (!ok) std::abort();
}

void check_value(const std::optional<double>& v, double limit) {
    if (v) check(std::isfinite(*v) && std::fabs(*v) <= limit);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view body(reinterpret_cast<const char*>(data), size);
    ng::Sentence s;
    const ng::DecodeResult r = ng::decode(body, s);
    if (r.error != ng::DecodeError::kNone) return 0;

    check(body.size() >= 5);
    check(s.talker[2] == '\0' && s.type_code[3] == '\0');
    switch (s.type) {
        case ng::SentenceType::kGGA:
            check_value(s.gga.latitude, 90.0);
            check_value(s.gga.longitude, 180.0);
            check_value(s.gga.hdop, 1e300);
            break;
        case ng::SentenceType::kRMC:
            check_value(s.rmc.latitude, 90.0);
            check_value(s.rmc.longitude, 180.0);
            check_value(s.rmc.speed_knots, 1e300);
            break;
        case ng::SentenceType::kGSA:
            check(s.gsa.prn_count <= ng::Gsa::kMaxPrns);
            check(!s.gsa.fix_type || (*s.gsa.fix_type >= 1 && *s.gsa.fix_type <= 3));
            break;
        case ng::SentenceType::kVTG:
            check_value(s.vtg.speed_kmh, 1e300);
            break;
        case ng::SentenceType::kUnknown:
            check(false);
    }
    return 0;
}
