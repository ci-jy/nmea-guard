#include "nmea_guard/sentences.hpp"

namespace nmea_guard {

namespace {

bool is_upper_alnum(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// Tracks the first failing field so callers can report where bytes went missing.
struct Checker {
    const FieldList& f;
    DecodeResult result;

    bool ok() const noexcept { return result.error == DecodeError::kNone; }
    void check(bool good, std::size_t index) noexcept {
        if (!good && ok()) {
            result.error = DecodeError::kBadField;
            result.field = static_cast<std::uint8_t>(index);
        }
    }
    // Unit letters such as the 'M' after altitude or 'T' after course must be
    // the expected letter or blank.
    void unit(std::size_t index, char expected) noexcept {
        const std::string_view s = f[index];
        check(s.empty() || (s.size() == 1 && s[0] == expected), index);
    }
};

DecodeResult decode_gga(const FieldList& f, Gga& g) noexcept {
    Checker c{f, {}};
    c.check(field::parse_time(f[1], g.time), 1);
    c.check(field::parse_latitude(f[2], f[3], g.latitude), 2);
    c.check(field::parse_longitude(f[4], f[5], g.longitude), 4);
    c.check(field::parse_uint(f[6], g.quality), 6);
    c.check(!g.quality || *g.quality <= 9, 6);
    c.check(field::parse_uint(f[7], g.satellites), 7);
    c.check(field::parse_decimal(f[8], g.hdop), 8);
    c.check(field::parse_decimal(f[9], g.altitude_m), 9);
    c.unit(10, 'M');
    c.check(field::parse_decimal(f[11], g.geoid_separation_m), 11);
    c.unit(12, 'M');
    c.check(field::parse_decimal(f[13], g.dgps_age_s), 13);
    return c.result;
}

DecodeResult decode_rmc(const FieldList& f, Rmc& r) noexcept {
    Checker c{f, {}};
    c.check(field::parse_time(f[1], r.time), 1);
    c.check(field::parse_flag(f[2], "AV", r.status), 2);
    c.check(field::parse_latitude(f[3], f[4], r.latitude), 3);
    c.check(field::parse_longitude(f[5], f[6], r.longitude), 5);
    c.check(field::parse_decimal(f[7], r.speed_knots), 7);
    c.check(field::parse_decimal(f[8], r.course_deg), 8);
    c.check(field::parse_date(f[9], r.date), 9);
    std::optional<double> var;
    c.check(field::parse_decimal(f[10], var), 10);
    // The variation direction should be E or W, but real receivers send 'N'
    // or drop the field and shift the mode letter into it. Any single letter
    // is tolerated; only 'W' changes the sign.
    std::optional<char> var_dir;
    c.check(field::parse_flag(f[11], "ABCDEFGHIJKLMNOPQRSTUVWXYZ", var_dir), 11);
    if (var) r.magnetic_variation_deg = (var_dir && *var_dir == 'W') ? -*var : *var;
    if (f.size() > 12) c.check(field::parse_flag(f[12], "ADEMNSFRPC", r.mode), 12);
    return c.result;
}

DecodeResult decode_gsa(const FieldList& f, Gsa& g) noexcept {
    Checker c{f, {}};
    c.check(field::parse_flag(f[1], "AM", g.selection), 1);
    c.check(field::parse_uint(f[2], g.fix_type), 2);
    c.check(!g.fix_type || (*g.fix_type >= 1 && *g.fix_type <= 3), 2);
    // Twelve PRN slots, blank when unused. Some receivers send more than
    // twelve; those extra slots shift the DOP fields, so we locate DOPs from
    // the end of the standard layout and ignore surplus PRNs.
    for (std::size_t i = 3; i < 15; ++i) {
        std::optional<std::uint32_t> prn;
        c.check(field::parse_uint(f[i], prn), i);
        c.check(!prn || *prn <= 999, i);
        if (prn && g.prn_count < Gsa::kMaxPrns) g.prns[g.prn_count++] = static_cast<std::uint16_t>(*prn);
    }
    c.check(field::parse_decimal(f[15], g.pdop), 15);
    c.check(field::parse_decimal(f[16], g.hdop), 16);
    c.check(field::parse_decimal(f[17], g.vdop), 17);
    // NMEA 4.1 system ID. Some receivers put other data here, so a value that
    // is not an integer is ignored rather than treated as damage.
    if (f.size() > 18 && !field::parse_uint(f[18], g.system_id)) g.system_id.reset();
    return c.result;
}

DecodeResult decode_vtg(const FieldList& f, Vtg& v) noexcept {
    Checker c{f, {}};
    c.check(field::parse_decimal(f[1], v.course_true_deg), 1);
    c.unit(2, 'T');
    c.check(field::parse_decimal(f[3], v.course_magnetic_deg), 3);
    c.unit(4, 'M');
    c.check(field::parse_decimal(f[5], v.speed_knots), 5);
    c.unit(6, 'N');
    c.check(field::parse_decimal(f[7], v.speed_kmh), 7);
    c.unit(8, 'K');
    if (f.size() > 9) c.check(field::parse_flag(f[9], "ADEMNSFRPC", v.mode), 9);
    return c.result;
}

}  // namespace

const char* to_string(SentenceType t) noexcept {
    switch (t) {
        case SentenceType::kGGA: return "GGA";
        case SentenceType::kRMC: return "RMC";
        case SentenceType::kGSA: return "GSA";
        case SentenceType::kVTG: return "VTG";
        case SentenceType::kUnknown: break;
    }
    return "unknown";
}

const char* to_string(DecodeError e) noexcept {
    switch (e) {
        case DecodeError::kNone: return "none";
        case DecodeError::kBadAddress: return "bad_address";
        case DecodeError::kFieldCount: return "field_count";
        case DecodeError::kBadField: return "bad_field";
        case DecodeError::kUnsupported: return "unsupported";
    }
    return "unknown";
}

DecodeResult decode(std::string_view body, Sentence& out) noexcept {
    out.talker[0] = out.type_code[0] = '\0';
    out.type = SentenceType::kUnknown;

    FieldList f;
    if (!f.split(body)) return {DecodeError::kFieldCount, 0};

    // Standard addresses are five characters: a two-letter talker ID and a
    // three-letter sentence formatter. Proprietary 'P' sentences are left alone.
    const std::string_view addr = f[0];
    if (addr.size() != 5) return {DecodeError::kBadAddress, 0};
    for (char ch : addr)
        if (!is_upper_alnum(ch)) return {DecodeError::kBadAddress, 0};
    if (addr[0] == 'P') return {DecodeError::kUnsupported, 0};
    out.talker[0] = addr[0];
    out.talker[1] = addr[1];
    out.talker[2] = '\0';
    for (std::size_t i = 0; i < 3; ++i) out.type_code[i] = addr[2 + i];
    out.type_code[3] = '\0';

    const std::string_view code = addr.substr(2);
    // Minimum field counts include the address. Older receivers omit the
    // trailing fields added in NMEA 2.3 and 4.1, newer ones append fields, so
    // only a short sentence is an error.
    if (code == "GGA") {
        out.type = SentenceType::kGGA;
        out.gga = Gga{};
        if (f.size() < 14) return {DecodeError::kFieldCount, 0};
        return decode_gga(f, out.gga);
    }
    if (code == "RMC") {
        out.type = SentenceType::kRMC;
        out.rmc = Rmc{};
        if (f.size() < 12) return {DecodeError::kFieldCount, 0};
        return decode_rmc(f, out.rmc);
    }
    if (code == "GSA") {
        out.type = SentenceType::kGSA;
        out.gsa = Gsa{};
        if (f.size() < 18) return {DecodeError::kFieldCount, 0};
        return decode_gsa(f, out.gsa);
    }
    if (code == "VTG") {
        out.type = SentenceType::kVTG;
        out.vtg = Vtg{};
        if (f.size() < 9) return {DecodeError::kFieldCount, 0};
        return decode_vtg(f, out.vtg);
    }
    return {DecodeError::kUnsupported, 0};
}

}  // namespace nmea_guard
