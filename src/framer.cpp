#include "nmea_guard/framer.hpp"

namespace nmea_guard {

namespace {

int hex_value(std::uint8_t c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool is_line_end(std::uint8_t c) noexcept { return c == '\r' || c == '\n'; }

}  // namespace

const char* to_string(FrameError e) noexcept {
    switch (e) {
        case FrameError::kNone: return "none";
        case FrameError::kMissingChecksum: return "missing_checksum";
        case FrameError::kMalformedChecksum: return "malformed_checksum";
        case FrameError::kChecksumMismatch: return "checksum_mismatch";
        case FrameError::kOverflow: return "overflow";
        case FrameError::kInterrupted: return "interrupted";
        case FrameError::kBadCharacter: return "bad_character";
        case FrameError::kTooShort: return "too_short";
    }
    return "unknown";
}

void Framer::reset() noexcept {
    clear();
    pending_start_ = false;
}

void Framer::clear() noexcept {
    // pending_start_ is left alone: a sentence rejected on a '$' still owes
    // the next sentence its start marker.
    state_ = State::kHunt;
    running_ = 0;
    expected_ = 0;
    body_len_ = 0;
    raw_len_ = 0;
}

FeedResult Framer::reject(FrameError e) noexcept {
    error_ = e;
    clear();
    return FeedResult::kRejected;
}

bool Framer::push_raw(std::uint8_t byte) noexcept {
    if (raw_len_ >= kCapacity) return false;
    raw_[raw_len_++] = static_cast<char>(byte);
    return true;
}

FeedResult Framer::finish() noexcept {
    if (expected_ != running_) return reject(FrameError::kChecksumMismatch);
    // "GPGGA" is the shortest useful header: two-letter talker, three-letter type.
    if (body_len_ < 5) return reject(FrameError::kTooShort);
    error_ = FrameError::kNone;
    state_ = State::kHunt;
    return FeedResult::kSentence;
}

FeedResult Framer::feed(std::uint8_t byte) noexcept {
    if (pending_start_) {
        // The previous call finished a sentence on a '$'; that '$' opens this one.
        pending_start_ = false;
        clear();
        state_ = State::kBody;
        push_raw('$');
    }

    if (byte == '$' && state_ == State::kEnd) {
        // "*hh" was complete but the line end was lost. The checksum covers
        // the whole sentence, so it can still be judged; the '$' starts the
        // next sentence once the caller has had a chance to read this one.
        pending_start_ = true;
        return finish();
    }

    if (byte == '$') {
        // A start marker always begins a fresh sentence. If one was already
        // in progress, it lost its tail (typically a UART overrun).
        const bool interrupted = state_ != State::kHunt;
        clear();
        state_ = State::kBody;
        push_raw(byte);
        if (interrupted) {
            error_ = FrameError::kInterrupted;
            return FeedResult::kRejected;
        }
        return FeedResult::kNone;
    }

    switch (state_) {
        case State::kHunt:
            return FeedResult::kNone;

        case State::kBody:
            if (byte == '*') {
                state_ = State::kSum1;
                push_raw(byte);
                return FeedResult::kNone;
            }
            if (is_line_end(byte)) return reject(FrameError::kMissingChecksum);
            if (byte < 0x20 || byte > 0x7e) return reject(FrameError::kBadCharacter);
            if (body_len_ >= kCapacity || !push_raw(byte)) return reject(FrameError::kOverflow);
            buf_[body_len_++] = static_cast<char>(byte);
            running_ ^= byte;
            return FeedResult::kNone;

        case State::kSum1:
        case State::kSum2: {
            const int v = hex_value(byte);
            if (v < 0) return reject(FrameError::kMalformedChecksum);
            if (!push_raw(byte)) return reject(FrameError::kOverflow);
            expected_ = static_cast<std::uint8_t>((expected_ << 4) | v);
            state_ = state_ == State::kSum1 ? State::kSum2 : State::kEnd;
            return FeedResult::kNone;
        }

        case State::kEnd:
            // Exactly two hex digits, then the line must end. Anything else
            // means the '*' we saw was not the real checksum delimiter.
            if (is_line_end(byte)) return finish();
            return reject(FrameError::kMalformedChecksum);
    }
    return FeedResult::kNone;
}

}  // namespace nmea_guard
