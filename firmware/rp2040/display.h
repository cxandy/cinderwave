// ILI9341 320x240 SPI display driver + a few drawing primitives.
//
// Panel wiring (see pins.h):
//   SCK = GP6, MOSI = GP7, DC = GP10, CS = GP13
//   RST is hard-jumpered to 3V3, so the panel is never hardware-reset and the
//   firmware does not drive a reset line at all.
//
// SPI0 runs in 8-bit mode: commands and parameters go out as plain bytes, and
// pixel data is pushed as big-endian RGB565 byte pairs. (pico-sdk's
// spi_write16_blocking would need a 16-bit frame format, which then breaks
// plain 8-bit command writes — mixing the two is a classic source of
// "every command byte is followed by a zero" bugs, so we stay in 8-bit mode.)
#ifndef CINDERWAVE_DISPLAY_H
#define CINDERWAVE_DISPLAY_H

#include <stdint.h>
#include <cstddef>

namespace cwf {

// Pack an 8-bit RGB triple into RGB565.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// A small palette so the UI code does not juggle magic numbers.
namespace color {
constexpr uint16_t kBlack   = 0x0000;
constexpr uint16_t kWhite   = 0xFFFF;
constexpr uint16_t kRed     = rgb565(0xF8, 0x30, 0x30);
constexpr uint16_t kGreen   = rgb565(0x30, 0xE0, 0x40);
constexpr uint16_t kBlue    = rgb565(0x30, 0x60, 0xF8);
constexpr uint16_t kCyan    = rgb565(0x30, 0xD0, 0xE0);
constexpr uint16_t kYellow  = rgb565(0xF8, 0xE0, 0x30);
constexpr uint16_t kMagenta = rgb565(0xE0, 0x40, 0xE0);
constexpr uint16_t kOrange  = rgb565(0xF8, 0x90, 0x20);
constexpr uint16_t kGrey    = rgb565(0x40, 0x40, 0x40);
constexpr uint16_t kDarkGrey = rgb565(0x20, 0x20, 0x20);
constexpr uint16_t kMidGrey = rgb565(0x80, 0x80, 0x80);
} // namespace color

class Display {
public:
    // Brings up SPI0 and the panel, then clears it to black.
    void init();

    int width() const;
    int height() const;

    void fillScreen(uint16_t c);
    void fillRect(int x, int y, int w, int h, uint16_t c);
    void drawRect(int x, int y, int w, int h, uint16_t c);

    // Draws one 5x7 glyph on an 8-row cell with an opaque background.
    void drawChar(int x, int y, char c, uint16_t fg, uint16_t bg);

    // Draws a NUL-terminated string, advancing 6 px per character.
    void drawText(int x, int y, const char* s, uint16_t fg, uint16_t bg);

    // Horizontal level meter; `t` is clamped to [0, 1].
    void drawBar(int x, int y, int w, int h, float t, uint16_t fg, uint16_t bg);

    // Re-reads the panel orientation from kTftRotation and re-applies it.
    void applyRotation();

private:
    void select();
    void deselect();
    void writeCommand(uint8_t cmd);
    void writeData(const uint8_t* data, size_t len);
    void writeCommand(uint8_t cmd, const uint8_t* params, size_t len);
    void setWindow(int x0, int y0, int x1, int y1);
};

} // namespace cwf

#endif // CINDERWAVE_DISPLAY_H
