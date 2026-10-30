#include "display.h"

#include "font5x7.h"
#include "pins.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

namespace cwf {
namespace {

// --- ILI9341 commands -------------------------------------------------
constexpr uint8_t kCmdSwReset  = 0x01;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdGammaSet = 0x26;
constexpr uint8_t kCmdDispOn   = 0x29;
constexpr uint8_t kCmdCaset    = 0x2A;
constexpr uint8_t kCmdRaset    = 0x2B;
constexpr uint8_t kCmdRamwr    = 0x2C;
constexpr uint8_t kCmdMadctl   = 0x36;
constexpr uint8_t kCmdPixelFmt = 0x3A;

// MADCTL per rotation, following the Adafruit_ILI9341 convention.
// Rotations 1 and 3 are the 320x240 landscape ones (MV exchanges the axes).
constexpr uint8_t kMadctl[4] = {0x48, 0x28, 0x88, 0xE8};

constexpr bool kLandscape = (cwp::kTftRotation & 1) != 0;
constexpr int kPanelW = kLandscape ? 320 : 240;
constexpr int kPanelH = kLandscape ? 240 : 320;

// Initialisation table, copied from the Adafruit_ILI9341 driver.
//
// Format: { command, len, args... }. If bit 7 of `len` is set, the driver
// pauses 150 ms after the command (needed for sleep-out and display-on).
constexpr uint8_t kInit[] = {
    0xEF, 3, 0x03, 0x80, 0x02,                 // command sequence control
    0xCF, 3, 0x00, 0xC1, 0x30,                 // power control B
    0xED, 4, 0x64, 0x03, 0x12, 0x81,           // power on sequence control
    0xE8, 3, 0x85, 0x00, 0x78,                 // driver timing control A
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,     // power control C
    0xF7, 1, 0x20,                             // ratio control
    0xEA, 2, 0x00, 0x00,                       // driver timing control B
    0xC0, 1, 0x23,                             // power control 1
    0xC1, 1, 0x10,                             // power control 2
    0xC5, 2, 0x3E, 0x28,                       // VCOM control 1
    0xC7, 1, 0x86,                             // VCOM control 2
    0x36, 1, 0x48,                             // memory access control (re-set below)
    0x37, 1, 0x00,                             // vertical scroll start
    0x3A, 1, 0x55,                             // pixel format: 16 bpp
    0xB1, 2, 0x00, 0x18,                       // frame rate control
    0xB6, 3, 0x08, 0x82, 0x27,                 // display function control
    0xF2, 1, 0x00,                             // 3-gamma disable
    0x26, 1, 0x01,                             // gamma set
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
             0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,  // positive gamma
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
             0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,  // negative gamma
    0x11, 0x80,                                // sleep out, then wait
    0x29, 0x80,                                // display on, then wait
    0x00,                                      // end of table
};

// One row of pixels in RGB565 big-endian, reused by fillRect().
uint8_t g_rowBytes[320 * 2];

} // namespace

int Display::width() const { return kPanelW; }
int Display::height() const { return kPanelH; }

void Display::select() { gpio_put(cwp::kTftCsPin, 0); }
void Display::deselect() { gpio_put(cwp::kTftCsPin, 1); }

void Display::writeCommand(uint8_t cmd) {
    gpio_put(cwp::kTftDcPin, 0);
    spi_write_blocking(spi0, &cmd, 1);
}

void Display::writeData(const uint8_t* data, size_t len) {
    gpio_put(cwp::kTftDcPin, 1);
    spi_write_blocking(spi0, data, len);
}

void Display::writeCommand(uint8_t cmd, const uint8_t* params, size_t len) {
    writeCommand(cmd);
    if (len) writeData(params, len);
}

void Display::setWindow(int x0, int y0, int x1, int y1) {
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(x0 >> 8);
    buf[1] = static_cast<uint8_t>(x0 & 0xFF);
    buf[2] = static_cast<uint8_t>(x1 >> 8);
    buf[3] = static_cast<uint8_t>(x1 & 0xFF);
    writeCommand(kCmdCaset, buf, 4);

    buf[0] = static_cast<uint8_t>(y0 >> 8);
    buf[1] = static_cast<uint8_t>(y0 & 0xFF);
    buf[2] = static_cast<uint8_t>(y1 >> 8);
    buf[3] = static_cast<uint8_t>(y1 & 0xFF);
    writeCommand(kCmdRaset, buf, 4);

    writeCommand(kCmdRamwr);
}

void Display::applyRotation() {
    const uint8_t madctl = kMadctl[cwp::kTftRotation & 3];
    select();
    writeCommand(kCmdMadctl, &madctl, 1);
    deselect();
}

void Display::init() {
    // SPI0 pins.
    gpio_set_function(cwp::kTftSckPin, GPIO_FUNC_SPI);
    gpio_set_function(cwp::kTftMosiPin, GPIO_FUNC_SPI);

    // Data/command and chip select.
    gpio_init(cwp::kTftDcPin);
    gpio_set_dir(cwp::kTftDcPin, GPIO_OUT);
    gpio_put(cwp::kTftDcPin, 0);

    gpio_init(cwp::kTftCsPin);
    gpio_set_dir(cwp::kTftCsPin, GPIO_OUT);
    gpio_put(cwp::kTftCsPin, 1);  // panel deselected

    spi_init(spi0, cwp::kTftSpiBaudrate);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    sleep_ms(20);

    // Software reset. There is no hardware reset pulse (RST is strapped to
    // 3V3), so this is what puts the controller into a known state.
    select();
    writeCommand(kCmdSwReset);
    deselect();
    sleep_ms(150);

    // The init table itself.
    size_t i = 0;
    while (kInit[i] != 0x00) {
        const uint8_t cmd = kInit[i++];
        const uint8_t lenByte = kInit[i++];
        const uint8_t len = lenByte & 0x7Fu;
        const bool delay = (lenByte & 0x80u) != 0;

        select();
        writeCommand(cmd, kInit + i, len);
        deselect();
        i += len;

        if (delay) sleep_ms(150);
    }

    applyRotation();

    fillScreen(color::kBlack);
}

void Display::fillScreen(uint16_t c) {
    fillRect(0, 0, width(), height(), c);
}

void Display::fillRect(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;

    // Clip to the visible area.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width()) w = width() - x;
    if (y + h > height()) h = height() - y;
    if (w <= 0 || h <= 0) return;

    const uint8_t hi = static_cast<uint8_t>(c >> 8);
    const uint8_t lo = static_cast<uint8_t>(c & 0xFFu);
    for (int i = 0; i < w; ++i) {
        g_rowBytes[i * 2] = hi;
        g_rowBytes[i * 2 + 1] = lo;
    }

    select();
    setWindow(x, y, x + w - 1, y + h - 1);
    for (int row = 0; row < h; ++row) {
        spi_write_blocking(spi0, g_rowBytes, static_cast<size_t>(w) * 2);
    }
    deselect();
}

void Display::drawRect(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    fillRect(x, y, w, 1, c);
    fillRect(x, y + h - 1, w, 1, c);
    fillRect(x, y, 1, h, c);
    fillRect(x + w - 1, y, 1, h, c);
}

void Display::drawChar(int x, int y, char ch, uint16_t fg, uint16_t bg) {
    const int code = static_cast<unsigned char>(ch);
    if (code < kFontFirstChar || code > kFontLastChar) return;
    if (x + kFontWidth <= 0 || y + kFontHeight <= 0) return;
    if (x >= width() || y >= height()) return;

    const uint8_t* glyph = &kFont5x7[(code - kFontFirstChar) * kFontWidth];

    // Lay the glyph out row-major so it can be pushed in one SPI burst.
    uint8_t buf[kFontWidth * kFontHeight * 2];
    for (int row = 0; row < kFontHeight; ++row) {
        for (int col = 0; col < kFontWidth; ++col) {
            const bool on = ((glyph[col] >> row) & 1u) != 0;
            const uint16_t px = on ? fg : bg;
            const int idx = (row * kFontWidth + col) * 2;
            buf[idx] = static_cast<uint8_t>(px >> 8);
            buf[idx + 1] = static_cast<uint8_t>(px & 0xFFu);
        }
    }

    const int w = (x + kFontWidth > width()) ? (width() - x) : kFontWidth;
    const int h = (y + kFontHeight > height()) ? (height() - y) : kFontHeight;

    select();
    setWindow(x, y, x + w - 1, y + h - 1);
    spi_write_blocking(spi0, buf, static_cast<size_t>(w) * kFontHeight * 2);
    deselect();
}

void Display::drawText(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
    if (!s) return;
    int cx = x;
    while (*s) {
        if (cx > width()) break;
        drawChar(cx, y, *s++, fg, bg);
        cx += kFontAdvance;
    }
}

void Display::drawBar(int x, int y, int w, int h, float t, uint16_t fg, uint16_t bg) {
    if (w <= 0 || h <= 0) return;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    fillRect(x, y, w, h, bg);
    int filled = static_cast<int>(t * static_cast<float>(w));
    if (filled > 0) fillRect(x, y, filled, h, fg);
}

} // namespace cwf
