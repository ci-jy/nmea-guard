// nmea-replay: feed recorded receiver logs through the parser byte by byte
// and report what came out.
//
//   nmea-replay [--parser guard|naive] [--sentences | --accepted] FILE...
//
// Default output is one JSON document with per-file and total counts.
// --sentences prints one JSON object per decoded sentence (JSON Lines).
// --accepted prints the raw text of every accepted sentence, one per line.

#include <charconv>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "naive_parser.hpp"
#include "nmea_guard/parser.hpp"

namespace ng = nmea_guard;

namespace {

enum class Output { kSummary, kSentences, kAccepted };

struct Options {
    bool naive = false;
    Output output = Output::kSummary;
    std::vector<std::string> files;
};

void json_string(std::FILE* out, std::string_view s) {
    std::fputc('"', out);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            std::fputc('\\', out);
            std::fputc(c, out);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            std::fprintf(out, "\\u%04x", c);
        } else {
            std::fputc(c, out);
        }
    }
    std::fputc('"', out);
}

void json_number(std::FILE* out, const std::optional<double>& v) {
    if (!v) {
        std::fputs("null", out);
        return;
    }
    // Shortest text that reads back as the same double.
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, *v);
    std::fwrite(buf, 1, static_cast<std::size_t>(r.ptr - buf), out);
}

void json_number(std::FILE* out, const std::optional<std::uint32_t>& v) {
    if (v) std::fprintf(out, "%u", *v);
    else std::fputs("null", out);
}

void json_char(std::FILE* out, const std::optional<char>& v) {
    if (v) std::fprintf(out, "\"%c\"", *v);
    else std::fputs("null", out);
}

void json_time(std::FILE* out, const std::optional<ng::UtcTime>& t) {
    if (!t) {
        std::fputs("null", out);
        return;
    }
    std::fprintf(out, "\"%02u:%02u:%02u.%06u\"", t->hour, t->minute, t->second, t->microsecond);
}

void json_date(std::FILE* out, const std::optional<ng::UtcDate>& d) {
    if (!d) {
        std::fputs("null", out);
        return;
    }
    std::fprintf(out, "{\"day\":%u,\"month\":%u,\"year\":%u}", d->day, d->month, d->year);
}

// Small helper so field lists read like the JSON they produce.
struct Obj {
    std::FILE* out;
    bool first = true;
    void key(const char* k) {
        std::fprintf(out, "%s\"%s\":", first ? "" : ",", k);
        first = false;
    }
};

void print_sentence(std::FILE* out, const std::string& file, const ng::Parser& p) {
    const ng::Sentence& s = p.sentence();
    Obj o{out};
    std::fputc('{', out);
    o.key("file"); json_string(out, file);
    o.key("raw"); json_string(out, p.raw());
    o.key("talker"); json_string(out, s.talker);
    o.key("type"); json_string(out, s.type_code);
    switch (s.type) {
        case ng::SentenceType::kGGA: {
            const ng::Gga& g = s.gga;
            o.key("time"); json_time(out, g.time);
            o.key("latitude"); json_number(out, g.latitude);
            o.key("longitude"); json_number(out, g.longitude);
            o.key("quality"); json_number(out, g.quality);
            o.key("satellites"); json_number(out, g.satellites);
            o.key("hdop"); json_number(out, g.hdop);
            o.key("altitude_m"); json_number(out, g.altitude_m);
            o.key("geoid_separation_m"); json_number(out, g.geoid_separation_m);
            o.key("dgps_age_s"); json_number(out, g.dgps_age_s);
            break;
        }
        case ng::SentenceType::kRMC: {
            const ng::Rmc& r = s.rmc;
            o.key("time"); json_time(out, r.time);
            o.key("status"); json_char(out, r.status);
            o.key("latitude"); json_number(out, r.latitude);
            o.key("longitude"); json_number(out, r.longitude);
            o.key("speed_knots"); json_number(out, r.speed_knots);
            o.key("course_deg"); json_number(out, r.course_deg);
            o.key("date"); json_date(out, r.date);
            o.key("magnetic_variation_deg"); json_number(out, r.magnetic_variation_deg);
            o.key("mode"); json_char(out, r.mode);
            break;
        }
        case ng::SentenceType::kGSA: {
            const ng::Gsa& g = s.gsa;
            o.key("selection"); json_char(out, g.selection);
            o.key("fix_type"); json_number(out, g.fix_type);
            o.key("prns");
            std::fputc('[', out);
            for (int i = 0; i < g.prn_count; ++i) std::fprintf(out, "%s%u", i ? "," : "", g.prns[i]);
            std::fputc(']', out);
            o.key("pdop"); json_number(out, g.pdop);
            o.key("hdop"); json_number(out, g.hdop);
            o.key("vdop"); json_number(out, g.vdop);
            o.key("system_id"); json_number(out, g.system_id);
            break;
        }
        case ng::SentenceType::kVTG: {
            const ng::Vtg& v = s.vtg;
            o.key("course_true_deg"); json_number(out, v.course_true_deg);
            o.key("course_magnetic_deg"); json_number(out, v.course_magnetic_deg);
            o.key("speed_knots"); json_number(out, v.speed_knots);
            o.key("speed_kmh"); json_number(out, v.speed_kmh);
            o.key("mode"); json_char(out, v.mode);
            break;
        }
        case ng::SentenceType::kUnknown:
            break;
    }
    std::fputs("}\n", out);
}

void print_fix(std::FILE* out, const ng::Fix& f) {
    Obj o{out};
    std::fputc('{', out);
    o.key("valid"); std::fputs(f.valid() ? "true" : "false", out);
    o.key("time"); json_time(out, f.time);
    o.key("date"); json_date(out, f.date);
    o.key("latitude"); json_number(out, f.latitude);
    o.key("longitude"); json_number(out, f.longitude);
    o.key("altitude_m"); json_number(out, f.altitude_m);
    o.key("speed_knots"); json_number(out, f.speed_knots);
    o.key("course_deg"); json_number(out, f.course_deg);
    o.key("quality"); json_number(out, f.quality);
    o.key("satellites"); json_number(out, f.satellites);
    o.key("fix_type"); json_number(out, f.fix_type);
    o.key("hdop"); json_number(out, f.hdop);
    o.key("pdop"); json_number(out, f.pdop);
    o.key("vdop"); json_number(out, f.vdop);
    std::fputc('}', out);
}

struct Summary {
    std::uint64_t bytes = 0, decoded = 0, other = 0, rejected = 0, epochs = 0, valid_fixes = 0;
    std::map<std::string, std::uint64_t> reasons, types, talkers;

    void add(const Summary& s) {
        bytes += s.bytes;
        decoded += s.decoded;
        other += s.other;
        rejected += s.rejected;
        epochs += s.epochs;
        valid_fixes += s.valid_fixes;
        for (const auto& [k, v] : s.reasons) reasons[k] += v;
        for (const auto& [k, v] : s.types) types[k] += v;
        for (const auto& [k, v] : s.talkers) talkers[k] += v;
    }
};

void print_map(std::FILE* out, const std::map<std::string, std::uint64_t>& m) {
    std::fputc('{', out);
    bool first = true;
    for (const auto& [k, v] : m) {
        std::fprintf(out, "%s", first ? "" : ",");
        json_string(out, k);
        std::fprintf(out, ":%llu", static_cast<unsigned long long>(v));
        first = false;
    }
    std::fputc('}', out);
}

void print_summary_fields(std::FILE* out, const Summary& s, bool naive) {
    std::fprintf(out, "\"bytes\":%llu,", static_cast<unsigned long long>(s.bytes));
    if (naive) {
        std::fprintf(out, "\"accepted\":%llu", static_cast<unsigned long long>(s.decoded));
        return;
    }
    std::fprintf(out, "\"accepted\":%llu,\"other\":%llu,\"rejected\":%llu,",
                 static_cast<unsigned long long>(s.decoded), static_cast<unsigned long long>(s.other),
                 static_cast<unsigned long long>(s.rejected));
    std::fputs("\"rejected_by_reason\":", out);
    print_map(out, s.reasons);
    std::fputs(",\"accepted_by_type\":", out);
    print_map(out, s.types);
    std::fputs(",\"accepted_by_talker\":", out);
    print_map(out, s.talkers);
    std::fprintf(out, ",\"epochs\":%llu,\"valid_fixes\":%llu", static_cast<unsigned long long>(s.epochs),
                 static_cast<unsigned long long>(s.valid_fixes));
}

// Returns false if the file could not be read.
bool replay(const Options& opt, const std::string& path, Summary& sum, bool& printed_any) {
    std::FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) {
        std::fprintf(stderr, "nmea-replay: cannot open %s\n", path.c_str());
        return false;
    }

    ng::Parser guard;
    ng::baseline::NaiveParser naive;
    unsigned char chunk[4096];
    std::size_t n;

    auto on_event = [&](ng::Event ev) {
        if (ev == ng::Event::kSentence) {
            ++sum.types[ng::to_string(guard.sentence().type)];
            ++sum.talkers[guard.sentence().talker];
            if (opt.output == Output::kSentences) print_sentence(stdout, path, guard);
            if (opt.output == Output::kAccepted) std::printf("%.*s\n", static_cast<int>(guard.raw().size()), guard.raw().data());
        } else if (ev == ng::Event::kRejected) {
            if (guard.last_frame_error() != ng::FrameError::kNone)
                ++sum.reasons[ng::to_string(guard.last_frame_error())];
            else
                ++sum.reasons[ng::to_string(guard.last_decode_error())];
        }
    };

    while ((n = std::fread(chunk, 1, sizeof chunk, in)) > 0) {
        for (std::size_t i = 0; i < n; ++i) {
            if (opt.naive) {
                ++sum.bytes;
                if (naive.feed(chunk[i])) {
                    ++sum.decoded;
                    if (opt.output == Output::kAccepted)
                        std::printf("%.*s\n", static_cast<int>(naive.line().size()), naive.line().data());
                }
            } else {
                on_event(guard.feed(chunk[i]));
            }
        }
    }
    std::fclose(in);

    if (opt.naive) {
        if (naive.feed('\n')) {
            ++sum.decoded;
            if (opt.output == Output::kAccepted)
                std::printf("%.*s\n", static_cast<int>(naive.line().size()), naive.line().data());
        }
    } else {
        on_event(guard.flush());
        const ng::Stats& st = guard.stats();
        sum.bytes = st.bytes;
        sum.decoded = st.decoded;
        sum.other = st.other;
        sum.rejected = st.rejected;
        sum.epochs = st.epochs;
        sum.valid_fixes = st.valid_fixes;
    }

    if (opt.output == Output::kSummary) {
        std::fputs(printed_any ? ",\n    {" : "\n    {", stdout);
        std::fputs("\"file\":", stdout);
        json_string(stdout, path);
        std::fputc(',', stdout);
        print_summary_fields(stdout, sum, opt.naive);
        if (!opt.naive) {
            std::fputs(",\"last_fix\":", stdout);
            if (guard.has_fix()) print_fix(stdout, guard.fix());
            else std::fputs("null", stdout);
        }
        std::fputc('}', stdout);
        printed_any = true;
    }
    return true;
}

int usage() {
    std::fputs("usage: nmea-replay [--parser guard|naive] [--sentences | --accepted] FILE...\n", stderr);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--parser" && i + 1 < argc) {
            const std::string v = argv[++i];
            if (v == "naive") opt.naive = true;
            else if (v != "guard") return usage();
        } else if (a == "--sentences") {
            opt.output = Output::kSentences;
        } else if (a == "--accepted") {
            opt.output = Output::kAccepted;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            return usage();
        } else {
            opt.files.push_back(a == "-" ? "/dev/stdin" : a);
        }
    }
    if (opt.files.empty()) return usage();
    if (opt.naive && opt.output == Output::kSentences) {
        std::fputs("nmea-replay: --sentences needs the guard parser\n", stderr);
        return 2;
    }

    Summary total;
    bool printed_any = false;
    bool ok = true;
    if (opt.output == Output::kSummary)
        std::printf("{\n  \"parser\": \"%s\",\n  \"files\": [", opt.naive ? "naive" : "guard");
    for (const std::string& f : opt.files) {
        Summary s;
        ok = replay(opt, f, s, printed_any) && ok;
        total.add(s);
    }
    if (opt.output == Output::kSummary) {
        std::printf("\n  ],\n  \"total\": {\"files\":%zu,", opt.files.size());
        print_summary_fields(stdout, total, opt.naive);
        std::fputs("}\n}\n", stdout);
    }
    return ok ? 0 : 1;
}
