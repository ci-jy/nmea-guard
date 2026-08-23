#include <gtest/gtest.h>

#include "nmea_guard/sentences.hpp"

using namespace nmea_guard;

namespace {

TEST(Decode, Gga) {
    Sentence s;
    const auto r = decode("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", s);
    ASSERT_EQ(r.error, DecodeError::kNone);
    EXPECT_EQ(s.type, SentenceType::kGGA);
    EXPECT_STREQ(s.talker, "GP");
    EXPECT_NEAR(*s.gga.latitude, 48.1173, 1e-9);
    EXPECT_NEAR(*s.gga.longitude, 11.516666666, 1e-8);
    EXPECT_EQ(*s.gga.quality, 1u);
    EXPECT_EQ(*s.gga.satellites, 8u);
    EXPECT_DOUBLE_EQ(*s.gga.hdop, 0.9);
    EXPECT_DOUBLE_EQ(*s.gga.altitude_m, 545.4);
    EXPECT_DOUBLE_EQ(*s.gga.geoid_separation_m, 46.9);
    EXPECT_FALSE(s.gga.dgps_age_s.has_value());
}

TEST(Decode, GgaWithoutFixHasEmptyFields) {
    Sentence s;
    const auto r = decode("GNGGA,074150.799,,,,,0,00,,,M,0.0,M,,0000", s);
    ASSERT_EQ(r.error, DecodeError::kNone);
    EXPECT_FALSE(s.gga.latitude.has_value());
    EXPECT_FALSE(s.gga.longitude.has_value());
    EXPECT_EQ(*s.gga.quality, 0u);
    EXPECT_FALSE(s.gga.altitude_m.has_value());
}

TEST(Decode, Rmc) {
    Sentence s;
    const auto r = decode("GNRMC,081411.000,A,5541.1955,N,03737.2872,E,0.60,301.29,260514,,,A", s);
    ASSERT_EQ(r.error, DecodeError::kNone);
    EXPECT_EQ(s.type, SentenceType::kRMC);
    EXPECT_STREQ(s.talker, "GN");
    EXPECT_EQ(*s.rmc.status, 'A');
    EXPECT_EQ(s.rmc.date->day, 26);
    EXPECT_EQ(s.rmc.date->month, 5);
    EXPECT_DOUBLE_EQ(*s.rmc.speed_knots, 0.60);
    EXPECT_DOUBLE_EQ(*s.rmc.course_deg, 301.29);
    EXPECT_EQ(*s.rmc.mode, 'A');
}

TEST(Decode, RmcPre23WithoutModeAndWestVariation) {
    Sentence s;
    const auto r = decode("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", s);
    ASSERT_EQ(r.error, DecodeError::kNone);
    EXPECT_DOUBLE_EQ(*s.rmc.magnetic_variation_deg, -3.1);
    EXPECT_FALSE(s.rmc.mode.has_value());
}

TEST(Decode, Gsa) {
    Sentence s;
    const auto r = decode("GNGSA,A,3,03,19,27,13,,,,,,,,,2.75,2.56,1.00", s);
    ASSERT_EQ(r.error, DecodeError::kNone);
    EXPECT_EQ(*s.gsa.selection, 'A');
    EXPECT_EQ(*s.gsa.fix_type, 3u);
    ASSERT_EQ(s.gsa.prn_count, 4);
    EXPECT_EQ(s.gsa.prns[0], 3);
    EXPECT_EQ(s.gsa.prns[3], 13);
    EXPECT_DOUBLE_EQ(*s.gsa.pdop, 2.75);
    EXPECT_DOUBLE_EQ(*s.gsa.vdop, 1.00);
    EXPECT_FALSE(s.gsa.system_id.has_value());
}

TEST(Decode, GsaWithSystemId) {
    Sentence s;
    ASSERT_EQ(decode("GNGSA,A,3,80,71,73,79,69,,,,,,,,1.83,1.09,1.47,2", s).error, DecodeError::kNone);
    EXPECT_EQ(*s.gsa.system_id, 2u);
}

TEST(Decode, Vtg) {
    Sentence s;
    ASSERT_EQ(decode("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A", s).error, DecodeError::kNone);
    EXPECT_DOUBLE_EQ(*s.vtg.course_true_deg, 54.7);
    EXPECT_DOUBLE_EQ(*s.vtg.course_magnetic_deg, 34.4);
    EXPECT_DOUBLE_EQ(*s.vtg.speed_knots, 5.5);
    EXPECT_DOUBLE_EQ(*s.vtg.speed_kmh, 10.2);
    EXPECT_EQ(*s.vtg.mode, 'A');
}

TEST(Decode, VtgOldFormatWithoutMode) {
    Sentence s;
    ASSERT_EQ(decode("GPVTG,87.21,T,,M,0.12,N,0.2,K", s).error, DecodeError::kNone);
    EXPECT_FALSE(s.vtg.course_magnetic_deg.has_value());
}

class TalkerIds : public ::testing::TestWithParam<const char*> {};

TEST_P(TalkerIds, DecodesSameSentenceForEveryTalker) {
    const std::string body = std::string(GetParam()) + "RMC,081411.000,A,5541.1955,N,03737.2872,E,0.60,301.29,260514,,,A";
    Sentence s;
    ASSERT_EQ(decode(body, s).error, DecodeError::kNone);
    EXPECT_EQ(s.type, SentenceType::kRMC);
    EXPECT_STREQ(s.talker, GetParam());
    EXPECT_NEAR(*s.rmc.latitude, 55.686591667, 1e-8);
}

INSTANTIATE_TEST_SUITE_P(Constellations, TalkerIds,
                         ::testing::Values("GP", "GN", "GL", "GA", "GB", "BD", "GQ"));

TEST(Decode, ProprietaryAndOtherTypesAreUnsupported) {
    Sentence s;
    EXPECT_EQ(decode("PGRME,15.0,M,45.0,M,25.0,M", s).error, DecodeError::kUnsupported);
    EXPECT_EQ(decode("GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00", s).error,
              DecodeError::kUnsupported);
    EXPECT_STREQ(s.type_code, "GSV");
}

TEST(Decode, RejectsBadAddress) {
    Sentence s;
    EXPECT_EQ(decode("GPGG,1", s).error, DecodeError::kBadAddress);
    EXPECT_EQ(decode("GPGGAX,1", s).error, DecodeError::kBadAddress);
    EXPECT_EQ(decode("gpgga,1", s).error, DecodeError::kBadAddress);
}

TEST(Decode, RejectsTooFewFields) {
    Sentence s;
    EXPECT_EQ(decode("GPGSA,A,1,,,", s).error, DecodeError::kFieldCount);
    EXPECT_EQ(decode("GPGGA,123519,4807.038,N", s).error, DecodeError::kFieldCount);
}

TEST(Decode, ReportsFirstBadField) {
    Sentence s;
    // Two characters of the latitude lost: "4807.038" became "07.038".
    const auto r = decode("GPGGA,123519,07.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", s);
    EXPECT_EQ(r.error, DecodeError::kBadField);
    EXPECT_EQ(r.field, 2);
}

TEST(Decode, RejectsShiftedFields) {
    // A comma lost in an overrun shifts every later field by one.
    Sentence s;
    const auto r = decode("GPRMC,123519,A,4807.038N,01131.000,E,022.4,084.4,230394,003.1,W,A", s);
    EXPECT_EQ(r.error, DecodeError::kBadField);
}

TEST(Decode, RejectsInvalidPlaceholderPositions) {
    // Some receivers print 89 degrees 60 minutes or 360 degrees while they
    // have no fix. Those values are not positions, so they are not decoded.
    Sentence s;
    EXPECT_EQ(decode("GPGGA,235946.005,8960.000000,N,00000.000000,E,0,0,,137.000,M,13.000,M,,", s).error,
              DecodeError::kBadField);
    EXPECT_EQ(decode("GPRMC,000416.984,V,36000.0000,N,72000.0000,E,0.000000,,260120,,", s).error,
              DecodeError::kBadField);
}

}  // namespace
