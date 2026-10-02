// Bare-metal test-vector runner. Streams every vector through one Parser,
// the way bytes arrive from a UART, and checks the event and the decoded
// values for each line. Then a few scenarios the vectors can't express
// (overflow, reset after an overrun, two sentences run together, fix
// assembly), and a stack measurement of the feed loop.
//
// The same file builds for the host (ctest) and for the Cortex-M3 image that
// `make -C firmware qemu-test` runs on QEMU's mps2-an385.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "nmea_guard/parser.hpp"
#include "platform.hpp"
#include "vectors.hpp"

using nmea_guard::Event;
using nmea_guard::Parser;
using nmea_guard::Sentence;
using nmea_guard::SentenceType;

namespace {

Parser g_parser;  // ~2 KB, so static rather than on the 16 KB stack

void put(const char* s) { runner::write(s); }

void put_u32(std::uint32_t v) {
    char buf[11];
    char* p = buf + sizeof(buf) - 1;
    *p = '\0';
    do {
        *--p = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    put(p);
}

void put_i32(std::int32_t v) {
    if (v < 0) {
        put("-");
        put_u32(static_cast<std::uint32_t>(-(v + 1)) + 1u);
    } else {
        put_u32(static_cast<std::uint32_t>(v));
    }
}

const char* expect_name(std::uint8_t e) {
    static const char* const kNames[] = {"rejected", "other", "GGA", "RMC", "GSA", "VTG"};
    return e < 6 ? kNames[e] : "?";
}

struct Tally {
    std::uint32_t passed = 0;
    std::uint32_t failed = 0;
    std::uint32_t values = 0;     // decoded doubles compared
    std::uint32_t bit_exact = 0;  // of those, identical to pynmea2's double
};

// Feed a line plus CR/LF and return the one event it produced. More than one
// event for a single line is itself a failure, reported as 0xFF.
std::uint8_t feed_line(Parser& p, const char* line) {
    std::uint8_t result = 0xFF;
    int events = 0;
    auto take = [&](Event e) {
        if (e == Event::kNone) return;
        ++events;
        if (e == Event::kRejected) {
            result = 0;
        } else if (e == Event::kOther) {
            result = 1;
        } else {
            result = static_cast<std::uint8_t>(1 + static_cast<std::uint8_t>(p.sentence().type));
        }
    };
    for (const char* c = line; *c != '\0'; ++c) take(p.feed(static_cast<std::uint8_t>(*c)));
    take(p.feed('\r'));
    take(p.feed('\n'));
    return events == 1 ? result : 0xFF;
}

template <typename T>
std::int32_t or_blank(const std::optional<T>& v) {
    return v ? static_cast<std::int32_t>(*v) : -1;
}

std::int32_t time_ms(const std::optional<nmea_guard::UtcTime>& t) {
    if (!t) return -1;
    return static_cast<std::int32_t>(((t->hour * 60u + t->minute) * 60u + t->second) * 1000u +
                                     t->microsecond / 1000u);
}

// The decoded sentence laid out the way vectors.hpp describes.
void flatten(const Sentence& s, std::int32_t& time, const std::optional<double>* (&value)[4],
             std::int32_t (&number)[2]) {
    static const std::optional<double> kNone;
    time = -1;
    for (auto& v : value) v = &kNone;
    number[0] = number[1] = -1;
    switch (s.type) {
        case SentenceType::kGGA:
            time = time_ms(s.gga.time);
            value[0] = &s.gga.latitude;
            value[1] = &s.gga.longitude;
            value[2] = &s.gga.altitude_m;
            value[3] = &s.gga.hdop;
            number[0] = or_blank(s.gga.quality);
            number[1] = or_blank(s.gga.satellites);
            break;
        case SentenceType::kRMC:
            time = time_ms(s.rmc.time);
            value[0] = &s.rmc.latitude;
            value[1] = &s.rmc.longitude;
            value[2] = &s.rmc.speed_knots;
            value[3] = &s.rmc.course_deg;
            if (s.rmc.date) number[0] = s.rmc.date->day * 10000 + s.rmc.date->month * 100 + s.rmc.date->year;
            number[1] = or_blank(s.rmc.status);
            break;
        case SentenceType::kGSA:
            value[0] = &s.gsa.pdop;
            value[1] = &s.gsa.hdop;
            value[2] = &s.gsa.vdop;
            number[0] = or_blank(s.gsa.fix_type);
            number[1] = s.gsa.prn_count;
            break;
        case SentenceType::kVTG:
            value[0] = &s.vtg.course_true_deg;
            value[1] = &s.vtg.course_magnetic_deg;
            value[2] = &s.vtg.speed_knots;
            value[3] = &s.vtg.speed_kmh;
            number[0] = or_blank(s.vtg.mode);
            break;
        case SentenceType::kUnknown:
            break;
    }
}

void fail(std::uint32_t index, const runner::Vector& v, const char* what) {
    put("FAIL vector ");
    put_u32(index);
    put(": ");
    put(what);
    put("\n  ");
    put(v.line);
    put("\n");
}

bool check_values(std::uint32_t index, const runner::Vector& v, const Sentence& s, Tally& tally) {
    std::int32_t time;
    const std::optional<double>* value[4];
    std::int32_t number[2];
    flatten(s, time, value, number);
    if (time != v.time_ms) {
        fail(index, v, "time");
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (number[i] != v.number[i]) {
            fail(index, v, i == 0 ? "first integer field" : "second integer field");
            put("  got ");
            put_i32(number[i]);
            put(", expected ");
            put_i32(v.number[i]);
            put("\n");
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const bool present = (v.mask >> i) & 1u;
        if (value[i]->has_value() != present) {
            fail(index, v, "a value is blank on one side only");
            return false;
        }
        if (!present) continue;
        ++tally.values;
        const double ours = **value[i];
        const double diff = ours > v.value[i] ? ours - v.value[i] : v.value[i] - ours;
        if (diff == 0.0) ++tally.bit_exact;
        // The host differential test allows 1e-12 degrees; so does this.
        if (diff > 1e-12) {
            fail(index, v, "decoded value differs from pynmea2");
            return false;
        }
    }
    return true;
}

Tally run_vectors(Parser& p) {
    Tally tally;
    for (std::uint32_t i = 0; i < runner::kVectorCount; ++i) {
        const runner::Vector& v = runner::kVectors[i];
        const std::uint8_t got = feed_line(p, v.line);
        bool ok = got == v.expect;
        if (!ok) {
            fail(i, v, "wrong event");
            put("  got ");
            put(got == 0xFF ? "no single event" : expect_name(got));
            put(", expected ");
            put(expect_name(v.expect));
            put("\n");
        } else if (v.expect >= 2) {
            ok = check_values(i, v, p.sentence(), tally);
        }
        ++(ok ? tally.passed : tally.failed);
    }
    return tally;
}

// ---------------------------------------------------------------- scenarios

const char kGga[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
const char kRmc[] = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
const char kGgaNext[] = "$GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*4D";

struct Events {
    std::uint32_t sentences = 0, other = 0, rejected = 0;
    nmea_guard::FrameError first_error = nmea_guard::FrameError::kNone;
};

void feed(Parser& p, const char* text, std::size_t n, Events& ev) {
    for (std::size_t i = 0; i < n; ++i) {
        switch (p.feed(static_cast<std::uint8_t>(text[i]))) {
            case Event::kSentence: ++ev.sentences; break;
            case Event::kOther: ++ev.other; break;
            case Event::kRejected:
                if (ev.rejected++ == 0) ev.first_error = p.last_frame_error();
                break;
            case Event::kNone: break;
        }
    }
}

void feed(Parser& p, const char* text, Events& ev) { feed(p, text, std::strlen(text), ev); }

bool overflow_then_recover(Parser& p) {
    Events ev;
    feed(p, "$GPGGA,", ev);
    for (int i = 0; i < 300; ++i) feed(p, "9", ev);
    feed(p, "\r\n", ev);
    feed(p, kGga, ev);
    feed(p, "\r\n", ev);
    return ev.rejected == 1 && ev.first_error == nmea_guard::FrameError::kOverflow && ev.sentences == 1 &&
           p.sentence().type == SentenceType::kGGA;
}

bool reset_after_overrun(Parser& p) {
    Events ev;
    feed(p, kGga, 30, ev);
    p.reset();  // the UART driver saw an overrun
    feed(p, kRmc, ev);
    feed(p, "\r\n", ev);
    return ev.rejected == 0 && ev.sentences == 1 && p.sentence().type == SentenceType::kRMC;
}

bool sentences_run_together(Parser& p) {
    // A read loop that fell behind: the head of a GGA meets a whole RMC.
    Events ev;
    feed(p, kGga, 40, ev);
    feed(p, kRmc, ev);
    feed(p, "\r\n", ev);
    return ev.rejected == 1 && ev.first_error == nmea_guard::FrameError::kInterrupted && ev.sentences == 1 &&
           p.sentence().type == SentenceType::kRMC && p.sentence().rmc.speed_knots &&
           *p.sentence().rmc.speed_knots == 22.4;
}

bool fix_from_one_epoch(Parser& p) {
    Events ev;
    for (const char* s : {kGga, kRmc, kGgaNext}) {
        feed(p, s, ev);
        feed(p, "\r\n", ev);
    }
    if (ev.sentences != 3 || !p.has_fix()) return false;
    const nmea_guard::Fix& f = p.fix();
    return f.valid() && f.satellites && *f.satellites == 8 && f.date && f.date->year == 94 && f.speed_knots &&
           *f.speed_knots == 22.4 && f.latitude && *f.latitude > 48.11729 && *f.latitude < 48.11731;
}

struct Scenario {
    const char* name;
    bool (*run)(Parser&);
};

const Scenario kScenarios[] = {
    {"overflow then recover", overflow_then_recover},
    {"reset after overrun", reset_after_overrun},
    {"sentences run together", sentences_run_together},
    {"fix from one epoch", fix_from_one_epoch},
};

void feed_all(void* arg) {
    Parser& p = *static_cast<Parser*>(arg);
    for (std::uint32_t i = 0; i < runner::kVectorCount; ++i) feed_line(p, runner::kVectors[i].line);
}

}  // namespace

int main() {
#if defined(__arm__)
    put("nmea-guard test vectors on Cortex-M3 (QEMU mps2-an385)\n");
#else
    put("nmea-guard test vectors on the host\n");
#endif
    const Tally tally = run_vectors(g_parser);

    std::uint32_t scenarios_passed = 0;
    for (const Scenario& s : kScenarios) {
        g_parser = Parser{};
        const bool ok = s.run(g_parser);
        put(ok ? "ok   " : "FAIL ");
        put(s.name);
        put("\n");
        scenarios_passed += ok ? 1u : 0u;
    }

    g_parser = Parser{};
    const std::size_t stack = runner::stack_used(feed_all, &g_parser);

    put("vectors ");
    put_u32(tally.passed);
    put("/");
    put_u32(runner::kVectorCount);
    put(" passed, ");
    put_u32(tally.values);
    put(" decoded values compared, ");
    put_u32(tally.bit_exact);
    put(" bit-exact\n");
    // One machine-readable line for scripts/firmware_report.py.
    put("RESULT vectors=");
    put_u32(runner::kVectorCount);
    put(" failed=");
    put_u32(tally.failed);
    put(" values=");
    put_u32(tally.values);
    put(" bit_exact=");
    put_u32(tally.bit_exact);
    put(" scenarios=");
    put_u32(scenarios_passed);
    put("/");
    put_u32(sizeof(kScenarios) / sizeof(kScenarios[0]));
    put(" sizeof_parser=");
    put_u32(sizeof(Parser));
    put(" stack_bytes=");
    put_u32(static_cast<std::uint32_t>(stack));
    put("\n");

    const bool ok = tally.failed == 0 && scenarios_passed == sizeof(kScenarios) / sizeof(kScenarios[0]);
    put(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
