// Decoders for the four sentences a position fix is built from.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "nmea_guard/fields.hpp"

namespace nmea_guard {

enum class SentenceType : std::uint8_t { kUnknown, kGGA, kRMC, kGSA, kVTG };

enum class DecodeError : std::uint8_t {
    kNone,
    kBadAddress,     // address is not two talker letters plus a type
    kFieldCount,     // fewer fields than the sentence type requires
    kBadField,       // a field is present but malformed
    kUnsupported,    // valid sentence of a type this library does not decode
};

const char* to_string(SentenceType t) noexcept;
const char* to_string(DecodeError e) noexcept;

// GGA: time, position and fix quality.
struct Gga {
    std::optional<UtcTime> time;
    std::optional<double> latitude;   // decimal degrees, south negative
    std::optional<double> longitude;  // decimal degrees, west negative
    std::optional<std::uint32_t> quality;
    std::optional<std::uint32_t> satellites;
    std::optional<double> hdop;
    std::optional<double> altitude_m;
    std::optional<double> geoid_separation_m;
    std::optional<double> dgps_age_s;
};

// RMC: recommended minimum, the only sentence that carries the date.
struct Rmc {
    std::optional<UtcTime> time;
    std::optional<char> status;  // 'A' valid, 'V' warning
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> speed_knots;
    std::optional<double> course_deg;
    std::optional<UtcDate> date;
    std::optional<double> magnetic_variation_deg;  // west negative
    std::optional<char> mode;                      // NMEA 2.3+
};

// GSA: dilution of precision and the satellites used.
struct Gsa {
    static constexpr int kMaxPrns = 12;
    std::optional<char> selection;               // 'A' auto, 'M' manual
    std::optional<std::uint32_t> fix_type;       // 1 none, 2 2D, 3 3D
    std::uint16_t prns[kMaxPrns] = {};
    std::uint8_t prn_count = 0;
    std::optional<double> pdop;
    std::optional<double> hdop;
    std::optional<double> vdop;
    std::optional<std::uint32_t> system_id;      // NMEA 4.1+
};

// VTG: course and speed over ground.
struct Vtg {
    std::optional<double> course_true_deg;
    std::optional<double> course_magnetic_deg;
    std::optional<double> speed_knots;
    std::optional<double> speed_kmh;
    std::optional<char> mode;
};

struct Sentence {
    char talker[3] = {};  // "GP", "GN", "GL", ... NUL-terminated
    char type_code[4] = {};
    SentenceType type = SentenceType::kUnknown;
    // Only the member matching `type` is meaningful. Plain members rather
    // than a union keep copies well defined; the whole struct is ~300 bytes.
    Gga gga;
    Rmc rmc;
    Gsa gsa;
    Vtg vtg;
};

struct DecodeResult {
    DecodeError error = DecodeError::kNone;
    std::uint8_t field = 0;  // index of the offending field for kBadField
};

// Decode a checksum-verified sentence body (no '$', no "*hh"). On error the
// contents of `out` are unspecified apart from talker and type_code.
DecodeResult decode(std::string_view body, Sentence& out) noexcept;

}  // namespace nmea_guard
