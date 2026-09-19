// libFuzzer target: arbitrary bytes into the streaming Parser.
//
// Besides crashes and sanitizer reports, it aborts when the parser breaks a
// promise the rest of the project relies on:
//   - an accepted sentence is well framed and its checksum really matches
//   - decoded values stay inside their physical ranges
//   - after any input, a reset() or a line end, the next good sentence comes
//     out whole and unmerged
//   - the statistics add up

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "nmea_guard/parser.hpp"

namespace ng = nmea_guard;

namespace {

constexpr std::string_view kGood =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

void check(bool ok) {
    if (!ok) std::abort();
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void check_raw(std::string_view raw) {
    check(raw.size() >= 4 && raw.size() <= ng::Framer::kCapacity);
    check(raw.front() == '$' && raw[raw.size() - 3] == '*');
    std::uint8_t sum = 0;
    for (std::size_t i = 1; i + 3 < raw.size(); ++i) {
        const auto c = static_cast<unsigned char>(raw[i]);
        check(c >= 0x20 && c < 0x7F && c != '$' && c != '*');
        sum = static_cast<std::uint8_t>(sum ^ c);
    }
    const int hi = hex(raw[raw.size() - 2]), lo = hex(raw[raw.size() - 1]);
    check(hi >= 0 && lo >= 0 && sum == ((hi << 4) | lo));
}

void check_angle(const std::optional<double>& v, double limit) {
    if (v) check(std::isfinite(*v) && std::fabs(*v) <= limit);
}

void check_time(const std::optional<ng::UtcTime>& t) {
    if (t) check(t->hour <= 23 && t->minute <= 59 && t->second <= 60 && t->microsecond < 1000000);
}

void check_sentence(const ng::Sentence& s) {
    switch (s.type) {
        case ng::SentenceType::kGGA:
            check_time(s.gga.time);
            check_angle(s.gga.latitude, 90.0);
            check_angle(s.gga.longitude, 180.0);
            break;
        case ng::SentenceType::kRMC:
            check_time(s.rmc.time);
            check_angle(s.rmc.latitude, 90.0);
            check_angle(s.rmc.longitude, 180.0);
            if (s.rmc.date) check(s.rmc.date->day >= 1 && s.rmc.date->day <= 31 &&
                                  s.rmc.date->month >= 1 && s.rmc.date->month <= 12);
            break;
        case ng::SentenceType::kGSA:
            check(s.gsa.prn_count <= ng::Gsa::kMaxPrns);
            break;
        case ng::SentenceType::kVTG:
            break;
        case ng::SentenceType::kUnknown:
            check(false);
    }
}

// Feeds bytes and checks every event; returns how many sentences were decoded.
std::uint32_t feed_all(ng::Parser& p, const std::uint8_t* data, std::size_t size, std::uint32_t& events) {
    std::uint32_t decoded = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const ng::Event ev = p.feed(data[i]);
        if (ev == ng::Event::kSentence || ev == ng::Event::kOther) {
            check_raw(p.raw());
            ++events;
        }
        if (ev == ng::Event::kSentence) {
            check_sentence(p.sentence());
            ++decoded;
        }
        if (ev == ng::Event::kRejected) ++events;
    }
    return decoded;
}

// The known-good sentence must come out exactly once, untouched.
void check_recovers(ng::Parser& p, std::string_view prefix) {
    std::uint32_t events = 0, good = 0;
    auto feed_text = [&](std::string_view text) {
        for (char c : text) {
            const ng::Event ev = p.feed(static_cast<std::uint8_t>(c));
            if (ev != ng::Event::kNone) ++events;
            if (ev == ng::Event::kSentence && p.raw() == kGood) ++good;
        }
    };
    feed_text(prefix);
    feed_text(kGood);
    feed_text("\r\n");
    check(good == 1);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    {
        ng::Parser p;
        std::uint32_t events = 0;
        const std::uint32_t decoded = feed_all(p, data, size, events);
        const ng::Stats& st = p.stats();
        check(st.bytes == size);
        check(st.decoded == decoded);
        check(st.decoded + st.other + st.rejected == events);
        std::uint32_t by_type = 0;
        for (std::uint32_t n : st.by_type) by_type += n;
        check(by_type == st.decoded);
        check(st.valid_fixes <= st.epochs);
        if (p.has_fix()) {
            check_angle(p.fix().latitude, 90.0);
            check_angle(p.fix().longitude, 180.0);
        }
        // Whatever state the input left behind, an overrun reset followed by
        // a good sentence must yield exactly that sentence.
        p.reset();
        check_recovers(p, "");
    }
    {
        // Same without a reset: a line end is enough to resynchronise.
        ng::Parser p;
        std::uint32_t events = 0;
        feed_all(p, data, size, events);
        check_recovers(p, "\r\n");
    }
    return 0;
}
