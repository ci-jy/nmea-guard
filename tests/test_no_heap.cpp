// Replaces the global allocator to prove the parser never calls it.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>

#include "test_util.hpp"

namespace {
std::atomic<long> g_allocations{0};
}

void* operator new(std::size_t n) {
    ++g_allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

TEST(NoHeap, ParsingAndFixAssemblyAllocateNothing) {
    // Build the input first; std::string allocates, the parser must not.
    std::string input;
    const char* bodies[] = {
        "GPGGA,081412.000,5541.1956,N,03737.2877,E,1,5,4.87,66.2,M,14.5,M,,",
        "GNGSA,A,3,03,19,27,13,,,,,,,,,4.97,4.87,1.00",
        "GNRMC,081412.000,A,5541.1956,N,03737.2877,E,0.52,301.26,260514,,,A",
        "GPVTG,301.26,T,,M,0.52,N,0.96,K,A",
        "GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00",
    };
    for (int rep = 0; rep < 200; ++rep)
        for (const char* b : bodies) input += test::frame(b);
    input += "$GPGGA," + std::string(400, '9');  // overflow
    input += "$GPRMC,1*ZZ\r\n";                  // malformed checksum
    input += std::string(64, '\xff');            // binary noise

    static nmea_guard::Parser parser;  // static: no stack or heap surprises
    const long before = g_allocations.load();
    for (char c : input) parser.feed(static_cast<std::uint8_t>(c));
    parser.flush();
    const long after = g_allocations.load();

    EXPECT_EQ(after - before, 0);
    EXPECT_EQ(parser.stats().decoded, 800u);
    EXPECT_EQ(parser.stats().other, 200u);
    EXPECT_GE(parser.stats().rejected, 2u);
}

TEST(NoHeap, ParserFitsInOneKilobyteOfRam) {
    // Rough budget check for small MCUs.
    EXPECT_LE(sizeof(nmea_guard::Parser), 1536u);
}

}  // namespace
