#include <gtest/gtest.h>

#include "test_util.hpp"

using namespace nmea_guard;
using test::feed_all;
using test::frame;

namespace {

const char* kEpoch1[] = {
    "GPGGA,081412.000,5541.1956,N,03737.2877,E,1,5,4.87,66.2,M,14.5,M,,",
    "GNGSA,A,3,03,19,27,13,,,,,,,,,4.97,4.87,1.00",
    "GNRMC,081412.000,A,5541.1956,N,03737.2877,E,0.52,301.26,260514,,,A",
    "GPVTG,301.26,T,,M,0.52,N,0.96,K,A",
};

TEST(Fix, MergesOneEpochAcrossSentenceTypes) {
    Parser p;
    for (const char* s : kEpoch1) feed_all(p, frame(s));
    EXPECT_FALSE(p.has_fix());  // epoch still open until the next timestamp
    p.flush();
    ASSERT_TRUE(p.has_fix());
    const Fix& f = p.fix();
    EXPECT_TRUE(f.valid());
    EXPECT_EQ(f.time->second, 12);
    EXPECT_EQ(f.date->day, 26);
    EXPECT_DOUBLE_EQ(*f.altitude_m, 66.2);
    EXPECT_EQ(*f.satellites, 5u);
    EXPECT_EQ(*f.fix_type, 3u);
    EXPECT_DOUBLE_EQ(*f.pdop, 4.97);
    EXPECT_DOUBLE_EQ(*f.speed_knots, 0.52);
    EXPECT_EQ(p.stats().epochs, 1u);
    EXPECT_EQ(p.stats().valid_fixes, 1u);
}

TEST(Fix, NewTimestampClosesPreviousEpoch) {
    Parser p;
    for (const char* s : kEpoch1) feed_all(p, frame(s));
    feed_all(p, frame("GPGGA,081413.000,5541.1959,N,03737.2894,E,1,5,4.87,66.3,M,14.5,M,,"));
    ASSERT_TRUE(p.has_fix());
    EXPECT_EQ(p.fix().time->second, 12);
    EXPECT_DOUBLE_EQ(*p.fix().altitude_m, 66.2);
    EXPECT_EQ(p.current_fix().time->second, 13);
    // Date only comes from RMC; it carries into the next epoch.
    EXPECT_EQ(p.current_fix().date->day, 26);
    EXPECT_FALSE(p.current_fix().speed_knots.has_value());
}

TEST(Fix, VoidRmcIsNotValid) {
    Parser p;
    feed_all(p, frame("GPRMC,212607.891,V,,,,,,,181207,,"));
    p.flush();
    ASSERT_TRUE(p.has_fix());
    EXPECT_FALSE(p.fix().valid());
    EXPECT_EQ(p.stats().valid_fixes, 0u);
}

TEST(Fix, GgaQualityDecidesValidityWithoutRmc) {
    Parser p;
    feed_all(p, frame("GPGGA,123519,4807.038,N,01131.000,E,0,00,,,M,,M,,"));
    p.flush();
    EXPECT_FALSE(p.fix().valid());
    feed_all(p, frame("GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
    p.flush();
    EXPECT_TRUE(p.fix().valid());
}

TEST(Fix, RejectedSentenceDoesNotTouchTheFix) {
    Parser p;
    for (const char* s : kEpoch1) feed_all(p, frame(s));
    std::string bad = frame("GPGGA,081412.000,5541.1956,N,03737.2877,E,1,5,4.87,99.9,M,14.5,M,,");
    bad[bad.find("99.9")] = '8';  // checksum now wrong
    feed_all(p, bad);
    p.flush();
    EXPECT_DOUBLE_EQ(*p.fix().altitude_m, 66.2);
    EXPECT_EQ(p.stats().rejected, 1u);
}

}  // namespace
