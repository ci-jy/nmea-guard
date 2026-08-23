#include "nmea_guard/parser.hpp"

namespace nmea_guard {

void Parser::count_epoch(const Fix& f) noexcept {
    ++stats_.epochs;
    if (f.valid()) ++stats_.valid_fixes;
}

Event Parser::feed(std::uint8_t byte) noexcept {
    ++stats_.bytes;
    frame_error_ = FrameError::kNone;
    decode_error_ = DecodeError::kNone;

    switch (framer_.feed(byte)) {
        case FeedResult::kNone:
            return Event::kNone;
        case FeedResult::kRejected:
            frame_error_ = framer_.last_error();
            ++stats_.frame_errors[static_cast<std::size_t>(frame_error_)];
            ++stats_.rejected;
            return Event::kRejected;
        case FeedResult::kSentence:
            break;
    }

    const DecodeResult r = decode(framer_.sentence(), sentence_);
    if (r.error == DecodeError::kUnsupported) {
        ++stats_.other;
        return Event::kOther;
    }
    if (r.error != DecodeError::kNone) {
        decode_error_ = r.error;
        bad_field_ = r.field;
        ++stats_.decode_errors[static_cast<std::size_t>(r.error)];
        ++stats_.rejected;
        return Event::kRejected;
    }

    ++stats_.decoded;
    ++stats_.by_type[static_cast<std::size_t>(sentence_.type)];
    if (fixes_.update(sentence_)) count_epoch(fixes_.last_complete());
    return Event::kSentence;
}

Event Parser::flush() noexcept {
    // A log usually ends mid-line or right after the last sentence; give the
    // framer a line end so a complete final sentence is not lost.
    const Event ev = feed('\n');
    --stats_.bytes;
    if (fixes_.close()) count_epoch(fixes_.last_complete());
    return ev;
}

}  // namespace nmea_guard
