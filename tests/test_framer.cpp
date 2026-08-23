#include <gtest/gtest.h>

#include "test_util.hpp"

using nmea_guard::FrameError;
using test::frame;
using test::run_framer;

namespace {

const char* kGga = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";

TEST(Framer, AcceptsValidSentence) {
    const auto out = run_framer(frame(kGga));
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.sentences[0], kGga);
    EXPECT_TRUE(out.errors.empty());
}

TEST(Framer, RawKeepsDelimitersAndChecksum) {
    nmea_guard::Framer f;
    const std::string wire = frame(kGga);
    for (char c : wire) f.feed(static_cast<std::uint8_t>(c));
    EXPECT_EQ(std::string(f.raw()) + "\r\n", wire);
}

TEST(Framer, AcceptsLfOnlyAndCrOnlyLineEndings) {
    std::string s = frame(kGga);
    s.erase(s.size() - 2, 1);  // drop CR
    EXPECT_EQ(run_framer(s).sentences.size(), 1u);
    s = frame(kGga);
    s.pop_back();  // drop LF
    EXPECT_EQ(run_framer(s).sentences.size(), 1u);
}

TEST(Framer, AcceptsLowercaseHexChecksum) {
    std::string s = frame("GPVTG,,T,,M,0.0,N,0.0,K,N");  // checksum has letters
    const std::size_t star = s.find('*');
    for (std::size_t i = star + 1; i < star + 3; ++i)
        if (s[i] >= 'A' && s[i] <= 'F') s[i] = static_cast<char>(s[i] - 'A' + 'a');
    EXPECT_EQ(run_framer(s).sentences.size(), 1u);
}

TEST(Framer, IgnoresNoiseBetweenSentences) {
    const std::string s = "garbage\r\n\x01\x02" + frame(kGga) + "more noise" + frame(kGga);
    const auto out = run_framer(s);
    EXPECT_EQ(out.sentences.size(), 2u);
    EXPECT_TRUE(out.errors.empty());
}

TEST(Checksum, RejectsWrongChecksum) {
    std::string s = frame(kGga);
    s[s.find('*') + 2] = s[s.find('*') + 2] == '0' ? '1' : '0';
    const auto out = run_framer(s);
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kChecksumMismatch);
}

TEST(Checksum, RejectsMissingChecksum) {
    const auto out = run_framer(std::string("$") + kGga + "\r\n");
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kMissingChecksum);
}

TEST(Checksum, RejectsStarWithNoDigits) {
    const auto out = run_framer(std::string("$") + kGga + "*\r\n");
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kMalformedChecksum);
}

TEST(Checksum, RejectsSingleDigit) {
    std::string s = frame(kGga);
    s.erase(s.find('*') + 1, 1);
    const auto out = run_framer(s);
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kMalformedChecksum);
}

TEST(Checksum, RejectsNonHexDigits) {
    const auto out = run_framer(std::string("$") + kGga + "*G1\r\n");
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kMalformedChecksum);
}

TEST(Checksum, RejectsTrailingCharactersAfterChecksum) {
    std::string s = frame(kGga);
    s.insert(s.size() - 2, "7");
    const auto out = run_framer(s);
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kMalformedChecksum);
}

// The checksum covers everything between '$' and '*', wherever the '*' is.
// A sentence that lost its tail but whose surviving "*hh" happens to sit in
// the middle of the line must still be checked, not waved through.
TEST(Checksum, StarInMiddleIsCheckedNotSkipped) {
    const std::string full = frame(kGga);
    // Keep "$GPGGA,123519,4807.038,N" and splice the original "*hh" on.
    std::string cut = full.substr(0, full.find(",01131")) + full.substr(full.find('*'));
    const auto out = run_framer(cut);
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kChecksumMismatch);
}

TEST(Checksum, ShortValidSentenceWithStarEarlyIsAccepted) {
    // A legitimately short sentence has its '*' early in the line too.
    const auto out = run_framer(frame("GPTXT,01"));
    EXPECT_EQ(out.sentences.size(), 1u);
}

TEST(Checksum, SecondStarIsMalformed) {
    std::string s = std::string("$") + kGga;
    s += "*4*7\r\n";
    const auto out = run_framer(s);
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kMalformedChecksum);
}

TEST(Framer, RejectsControlCharactersInsideSentence) {
    std::string s = frame(kGga);
    s[10] = '\x05';
    const auto out = run_framer(s);
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kBadCharacter);
}

TEST(Framer, RejectsHighBitBytes) {
    std::string s = frame(kGga);
    s[12] = static_cast<char>(0xB5);
    const auto out = run_framer(s);
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kBadCharacter);
}

TEST(Framer, RejectsTooShortSentence) {
    const auto out = run_framer(frame("GP"));
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kTooShort);
}

TEST(Framer, DollarMidSentenceRestarts) {
    // First sentence lost its tail; the second must come through intact.
    const std::string first = frame(kGga);
    const std::string s = first.substr(0, 20) + frame(kGga);
    const auto out = run_framer(s);
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.sentences[0], kGga);
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kInterrupted);
}

TEST(Framer, DroppedLineEndDoesNotMergeSentences) {
    std::string a = frame(kGga);
    a.erase(a.size() - 2);  // CR/LF lost in an overrun
    const auto out = run_framer(a + frame(kGga));
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.sentences[0], kGga);
    ASSERT_EQ(out.errors.size(), 1u);
    // The first sentence was complete up to "*hh" and then saw '$'.
    EXPECT_EQ(out.errors[0], FrameError::kInterrupted);
}

TEST(Framer, EveryPossibleSingleByteDropIsRejected) {
    // Losing any one byte of a sentence must never produce a different
    // accepted sentence: either nothing comes out or the error is reported.
    const std::string good = frame(kGga);
    for (std::size_t i = 0; i < good.size(); ++i) {
        std::string damaged = good;
        damaged.erase(i, 1);
        const auto out = run_framer(damaged + frame("GPTXT,01"));
        const bool line_end = good[i] == '\r' || good[i] == '\n';
        for (const auto& s : out.sentences) {
            // Losing one of CR/LF leaves the sentence itself intact.
            const bool ok = s == "GPTXT,01" || (line_end && s == kGga);
            EXPECT_TRUE(ok) << "drop at " << i << " let through: " << s;
        }
        // The trailing sentence always survives the damage before it.
        ASSERT_FALSE(out.sentences.empty()) << "drop at " << i;
        EXPECT_EQ(out.sentences.back(), "GPTXT,01");
    }
}

TEST(Framer, ToStringCoversAllErrors) {
    EXPECT_STREQ(nmea_guard::to_string(FrameError::kChecksumMismatch), "checksum_mismatch");
    EXPECT_STREQ(nmea_guard::to_string(FrameError::kOverflow), "overflow");
    EXPECT_STREQ(nmea_guard::to_string(FrameError::kMissingChecksum), "missing_checksum");
}

}  // namespace
