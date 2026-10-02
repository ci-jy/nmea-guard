// Size probe: the smallest realistic firmware that uses the parser. Polls
// the mps2-an385's CMSDK UART0, feeds every byte to a static Parser and
// keeps the last valid position. It is only linked, never run; its map file
// is what the flash and RAM figures in the size report come from.
#include <cstdint>

#include "nmea_guard/parser.hpp"

namespace {

struct CmsdkUart {
    volatile std::uint32_t data;
    volatile std::uint32_t state;  // bit 1: a received byte is waiting
    volatile std::uint32_t ctrl;   // bit 1: receiver enable
    volatile std::uint32_t intstatus;
    volatile std::uint32_t bauddiv;
};

CmsdkUart& uart0() { return *reinterpret_cast<CmsdkUart*>(0x40004000u); }

nmea_guard::Parser gps;

}  // namespace

// Where the rest of the firmware would pick the position up.
volatile double g_latitude;
volatile double g_longitude;

int main() {
    uart0().bauddiv = 217;  // 9600 baud from 25 MHz, ignored by QEMU
    uart0().ctrl = 1u << 1;
    for (;;) {
        if ((uart0().state & (1u << 1)) == 0) continue;
        const auto byte = static_cast<std::uint8_t>(uart0().data);
        if (gps.feed(byte) == nmea_guard::Event::kSentence && gps.has_fix() && gps.fix().valid()) {
            g_latitude = *gps.fix().latitude;
            g_longitude = *gps.fix().longitude;
        }
    }
}
