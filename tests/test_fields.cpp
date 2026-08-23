#include <gtest/gtest.h>

#include "nmea_guard/fields.hpp"

using namespace nmea_guard;

namespace {

TEST(FieldList, SplitsIncludingEmptyFields) {
    FieldList f;
    ASSERT_TRUE(f.split("GPGSA,A,3,,,1.0"));
    ASSERT_EQ(f.size(), 6u);
    EXPECT_EQ(f[0], "GPGSA");
    EXPECT_EQ(f[3], "");
    EXPECT_EQ(f[5], "1.0");
    EXPECT_EQ(f[99], "");  // out of range reads as blank
}

TEST(FieldList, TrailingCommaGivesEmptyLastField) {
    FieldList f;
    ASSERT_TRUE(f.split("GPGGA,1,"));
    EXPECT_EQ(f.size(), 3u);
}

TEST(FieldList, RefusesTooManyFields) {
    std::string s = "GPXXX";
    for (int i = 0; i < 50; ++i) s += ",";
    FieldList f;
    EXPECT_FALSE(f.split(s));
}

TEST(Decimal, ParsesForms) {
    std::optional<double> v;
    ASSERT_TRUE(field::parse_decimal("545.4", v));
    EXPECT_DOUBLE_EQ(*v, 545.4);
    ASSERT_TRUE(field::parse_decimal("-30.40", v));
    EXPECT_DOUBLE_EQ(*v, -30.4);
    ASSERT_TRUE(field::parse_decimal(".5", v));
    EXPECT_DOUBLE_EQ(*v, 0.5);
    ASSERT_TRUE(field::parse_decimal("7.", v));
    EXPECT_DOUBLE_EQ(*v, 7.0);
    ASSERT_TRUE(field::parse_decimal("", v));
    EXPECT_FALSE(v.has_value());
}

TEST(Decimal, RejectsGarbage) {
    std::optional<double> v;
    for (const char* bad : {"1.2.3", "-", ".", "1e5", "+1", " 1", "1,0", "12a", "--1"}) {
        EXPECT_FALSE(field::parse_decimal(bad, v)) << bad;
    }
}

TEST(Decimal, MatchesCorrectlyRoundedValue) {
    std::optional<double> v;
    ASSERT_TRUE(field::parse_decimal("41.1955", v));
    EXPECT_EQ(*v, 41.1955);  // exact, not just close
    ASSERT_TRUE(field::parse_decimal("0.000001", v));
    EXPECT_EQ(*v, 0.000001);
}

TEST(Uint, ParsesAndRejects) {
    std::optional<std::uint32_t> v;
    ASSERT_TRUE(field::parse_uint("08", v));
    EXPECT_EQ(*v, 8u);
    EXPECT_FALSE(field::parse_uint("8.0", v));
    EXPECT_FALSE(field::parse_uint("-1", v));
    EXPECT_FALSE(field::parse_uint("1234567890", v));
}

TEST(Time, ParsesWithAndWithoutFraction) {
    std::optional<UtcTime> t;
    ASSERT_TRUE(field::parse_time("123519", t));
    EXPECT_EQ(t->hour, 12);
    EXPECT_EQ(t->minute, 35);
    EXPECT_EQ(t->second, 19);
    EXPECT_EQ(t->microsecond, 0u);
    ASSERT_TRUE(field::parse_time("081411.250", t));
    EXPECT_EQ(t->microsecond, 250000u);
    ASSERT_TRUE(field::parse_time("235959.5", t));
    EXPECT_EQ(t->microsecond, 500000u);
}

TEST(Time, RejectsOutOfRangeAndShort) {
    std::optional<UtcTime> t;
    for (const char* bad : {"12351", "246000", "126000", "123561", "1235.9", "123519,0", "123519.x"}) {
        EXPECT_FALSE(field::parse_time(bad, t)) << bad;
    }
}

TEST(Date, ParsesAndRejects) {
    std::optional<UtcDate> d;
    ASSERT_TRUE(field::parse_date("260514", d));
    EXPECT_EQ(d->day, 26);
    EXPECT_EQ(d->month, 5);
    EXPECT_EQ(d->year, 14);
    for (const char* bad : {"2605141", "26051", "321214", "001214", "011314", "2208-1"}) {
        EXPECT_FALSE(field::parse_date(bad, d)) << bad;
    }
}

TEST(Latitude, ConvertsToSignedDegrees) {
    std::optional<double> v;
    ASSERT_TRUE(field::parse_latitude("4807.038", "N", v));
    EXPECT_DOUBLE_EQ(*v, 48.0 + 7.038 / 60.0);
    ASSERT_TRUE(field::parse_latitude("3345.1234", "S", v));
    EXPECT_LT(*v, 0.0);
    ASSERT_TRUE(field::parse_latitude("", "", v));
    EXPECT_FALSE(v.has_value());
}

TEST(Latitude, RejectsWrongDigitCountsAndRanges) {
    std::optional<double> v;
    EXPECT_FALSE(field::parse_latitude("807.038", "N", v));     // a dropped degree digit
    EXPECT_FALSE(field::parse_latitude("48007.038", "N", v));   // one too many
    EXPECT_FALSE(field::parse_latitude("4860.000", "N", v));    // 60 minutes
    EXPECT_FALSE(field::parse_latitude("9100.000", "N", v));    // past the pole
    EXPECT_FALSE(field::parse_latitude("4807.038", "E", v));    // wrong hemisphere letter
    EXPECT_FALSE(field::parse_latitude("4807.038", "", v));     // hemisphere missing
    EXPECT_FALSE(field::parse_latitude("", "N", v));            // value missing
    EXPECT_FALSE(field::parse_latitude("48a7.038", "N", v));
}

TEST(Longitude, ConvertsAndValidates) {
    std::optional<double> v;
    ASSERT_TRUE(field::parse_longitude("01131.000", "E", v));
    EXPECT_DOUBLE_EQ(*v, 11.0 + 31.0 / 60.0);
    ASSERT_TRUE(field::parse_longitude("08704.08478", "W", v));
    EXPECT_LT(*v, 0.0);
    EXPECT_FALSE(field::parse_longitude("1131.000", "E", v));
    EXPECT_FALSE(field::parse_longitude("18100.000", "E", v));
    EXPECT_FALSE(field::parse_longitude("01131.000", "N", v));
}

TEST(Flag, AcceptsOnlyAllowedSingleCharacters) {
    std::optional<char> c;
    ASSERT_TRUE(field::parse_flag("A", "AV", c));
    EXPECT_EQ(*c, 'A');
    ASSERT_TRUE(field::parse_flag("", "AV", c));
    EXPECT_FALSE(c.has_value());
    EXPECT_FALSE(field::parse_flag("X", "AV", c));
    EXPECT_FALSE(field::parse_flag("AV", "AV", c));
}

}  // namespace
