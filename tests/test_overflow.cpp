#include <gtest/gtest.h>

#include "test_util.hpp"

using namespace nmea_guard;
using test::frame;
using test::run_framer;

namespace {

const char* kRmc = "GNRMC,081411.000,A,5541.1955,N,03737.2872,E,0.60,301.29,260514,,,A";

TEST(Overflow, LongSentenceIsRejectedNotTruncated) {
    const std::string body = "GPTXT," + std::string(Framer::kCapacity + 10, 'X');
    const auto out = run_framer(frame(body));
    EXPECT_TRUE(out.sentences.empty());
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kOverflow);
}

TEST(Overflow, SentenceAtCapacityStillFits) {
    // '$' plus body plus "*hh" must fit the raw buffer.
    const std::string body = "GPTXT," + std::string(Framer::kCapacity - 4 - 6, 'X');
    const auto out = run_framer(frame(body));
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.sentences[0], body);
}

TEST(Overflow, RecoversOnNextSentence) {
    // Line end lost on a run of garbage: the framer overflows, then must pick
    // up the next sentence intact rather than gluing the two together.
    const std::string junk = "$GPGGA," + std::string(300, '7');
    const auto out = run_framer(junk + frame(kRmc));
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.sentences[0], kRmc);
    ASSERT_EQ(out.errors.size(), 1u);
    EXPECT_EQ(out.errors[0], FrameError::kOverflow);
}

TEST(Overflow, TailAfterOverflowIsNotParsedAsASentence) {
    // After the overflow, the rest of the long line ends with something that
    // looks like a checksum. It must be ignored because no '$' preceded it.
    const std::string junk = "$GPGGA," + std::string(200, '1') + "*00\r\n";
    const auto out = run_framer(junk + frame(kRmc));
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.errors.size(), 1u);
}

TEST(Overflow, TwoSentencesWithMissingTailsDoNotMerge) {
    std::string a = frame(kRmc);
    a.erase(a.size() - 6);
    std::string b = frame(kRmc);
    b.erase(b.size() - 9);
    const auto out = run_framer(a + b + frame(kRmc));
    ASSERT_EQ(out.sentences.size(), 1u);
    EXPECT_EQ(out.errors.size(), 2u);
}

TEST(Overflow, ParserStatsCountOverflowAndResumeDecoding) {
    Parser p;
    test::feed_all(p, "$GPGGA," + std::string(500, '5'));
    test::feed_all(p, frame(kRmc));
    EXPECT_EQ(p.stats().frame_errors[static_cast<int>(FrameError::kOverflow)], 1u);
    EXPECT_EQ(p.stats().decoded, 1u);
    EXPECT_EQ(p.sentence().type, SentenceType::kRMC);
}

TEST(Overflow, ExplicitResetDropsPartialSentence) {
    Framer f;
    const std::string s = frame(kRmc);
    for (std::size_t i = 0; i < 30; ++i) f.feed(static_cast<std::uint8_t>(s[i]));
    f.reset();  // e.g. the UART driver reported an overrun
    // The rest of the old sentence must not be accepted on its own.
    int sentences = 0;
    for (std::size_t i = 30; i < s.size(); ++i)
        sentences += f.feed(static_cast<std::uint8_t>(s[i])) == FeedResult::kSentence;
    EXPECT_EQ(sentences, 0);
    for (char c : s) sentences += f.feed(static_cast<std::uint8_t>(c)) == FeedResult::kSentence;
    EXPECT_EQ(sentences, 1);
}

}  // namespace
