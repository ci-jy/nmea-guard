#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "nmea_guard/framer.hpp"
#include "nmea_guard/parser.hpp"

namespace test {

// Wraps a body in '$', "*hh" and CR/LF with the correct checksum.
inline std::string frame(std::string_view body) {
    unsigned sum = 0;
    for (char c : body) sum ^= static_cast<unsigned char>(c);
    static const char* hex = "0123456789ABCDEF";
    std::string s = "$";
    s += body;
    s += '*';
    s += hex[(sum >> 4) & 0xF];
    s += hex[sum & 0xF];
    s += "\r\n";
    return s;
}

struct FrameOutcome {
    std::vector<std::string> sentences;
    std::vector<nmea_guard::FrameError> errors;
};

inline FrameOutcome run_framer(std::string_view bytes) {
    nmea_guard::Framer f;
    FrameOutcome out;
    for (char c : bytes) {
        switch (f.feed(static_cast<std::uint8_t>(c))) {
            case nmea_guard::FeedResult::kSentence: out.sentences.emplace_back(f.sentence()); break;
            case nmea_guard::FeedResult::kRejected: out.errors.push_back(f.last_error()); break;
            case nmea_guard::FeedResult::kNone: break;
        }
    }
    return out;
}

inline void feed_all(nmea_guard::Parser& p, std::string_view bytes) {
    for (char c : bytes) p.feed(static_cast<std::uint8_t>(c));
}

}  // namespace test
