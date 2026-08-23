// One-stop streaming parser: framing, checksum, decoding and fix assembly.
//
//   nmea_guard::Parser p;
//   while (uart_has_byte()) {
//       if (p.feed(uart_read()) == nmea_guard::Event::kSentence && p.fix().valid()) { ... }
//   }
//
// No heap, no exceptions, no RTTI. A Parser is about 1 KB and can live in a
// static or on the stack.
#pragma once

#include <cstddef>
#include <cstdint>

#include "nmea_guard/fix.hpp"
#include "nmea_guard/framer.hpp"
#include "nmea_guard/sentences.hpp"

namespace nmea_guard {

enum class Event : std::uint8_t {
    kNone,
    kSentence,  // sentence() is a decoded GGA/RMC/GSA/VTG
    kOther,     // checksum-valid sentence of a type we do not decode
    kRejected,  // see last_frame_error() / last_decode_error()
};

struct Stats {
    std::uint32_t bytes = 0;
    std::uint32_t decoded = 0;
    std::uint32_t other = 0;
    std::uint32_t rejected = 0;
    std::uint32_t epochs = 0;        // completed epochs
    std::uint32_t valid_fixes = 0;   // completed epochs with a valid position
    std::uint32_t by_type[5] = {};   // indexed by SentenceType
    std::uint32_t frame_errors[8] = {};   // indexed by FrameError
    std::uint32_t decode_errors[5] = {};  // indexed by DecodeError
};

class Parser {
public:
    Event feed(std::uint8_t byte) noexcept;
    // End of input: terminate a final unterminated line and close the epoch
    // in progress. Returns the event for that final line, if any.
    Event flush() noexcept;

    const Sentence& sentence() const noexcept { return sentence_; }
    std::string_view raw() const noexcept { return framer_.raw(); }
    const Fix& fix() const noexcept { return fixes_.last_complete(); }
    const Fix& current_fix() const noexcept { return fixes_.current(); }
    bool has_fix() const noexcept { return fixes_.has_complete(); }
    FrameError last_frame_error() const noexcept { return frame_error_; }
    DecodeError last_decode_error() const noexcept { return decode_error_; }
    std::uint8_t last_bad_field() const noexcept { return bad_field_; }
    const Stats& stats() const noexcept { return stats_; }

private:
    void count_epoch(const Fix& f) noexcept;

    Framer framer_;
    Sentence sentence_;
    FixAccumulator fixes_;
    Stats stats_;
    FrameError frame_error_ = FrameError::kNone;
    DecodeError decode_error_ = DecodeError::kNone;
    std::uint8_t bad_field_ = 0;
};

}  // namespace nmea_guard
