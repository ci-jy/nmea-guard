// Byte-at-a-time NMEA 0183 sentence framer.
//
// The framer owns a fixed buffer and never allocates. Feed it one byte at a
// time from a UART ISR, a DMA ring or a file; it reports a complete, checksum-
// verified sentence or a rejection with a reason. After any rejection it goes
// back to hunting for the next '$', so a damaged sentence can never be glued
// onto the one that follows it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nmea_guard {

enum class FeedResult : std::uint8_t {
    kNone,      // byte consumed, nothing to report yet
    kSentence,  // sentence() holds a verified sentence
    kRejected,  // last_error() says why the sentence in progress was dropped
};

enum class FrameError : std::uint8_t {
    kNone,
    kMissingChecksum,    // line ended without a '*'
    kMalformedChecksum,  // '*' not followed by exactly two hex digits
    kChecksumMismatch,   // checksum is well formed but wrong
    kOverflow,           // sentence longer than the buffer
    kInterrupted,        // a new '$' arrived before the line ended
    kBadCharacter,       // control or non-ASCII byte inside a sentence
    kTooShort,           // no room for a talker and sentence type
};

const char* to_string(FrameError e) noexcept;

class Framer {
public:
    // NMEA 0183 caps a sentence at 82 characters including '$' and CR/LF.
    // Real receivers overshoot it (long proprietary and NMEA 4.x sentences),
    // so the buffer leaves some headroom.
    static constexpr std::size_t kCapacity = 120;

    FeedResult feed(std::uint8_t byte) noexcept;
    void reset() noexcept;

    // Sentence text without the leading '$' and without "*hh". Valid only
    // until the next feed() call.
    std::string_view sentence() const noexcept { return {buf_, body_len_}; }
    // Same sentence including '$' and "*hh", as it appeared on the wire.
    std::string_view raw() const noexcept { return {raw_, raw_len_}; }
    FrameError last_error() const noexcept { return error_; }

private:
    enum class State : std::uint8_t { kHunt, kBody, kSum1, kSum2, kEnd };

    FeedResult reject(FrameError e) noexcept;
    FeedResult finish() noexcept;
    bool push_raw(std::uint8_t byte) noexcept;

    State state_ = State::kHunt;
    FrameError error_ = FrameError::kNone;
    std::uint8_t running_ = 0;   // XOR of body characters
    std::uint8_t expected_ = 0;  // value of the transmitted checksum
    std::size_t body_len_ = 0;
    std::size_t raw_len_ = 0;
    char buf_[kCapacity] = {};
    char raw_[kCapacity] = {};
};

}  // namespace nmea_guard
