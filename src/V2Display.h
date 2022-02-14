// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace V2Display {
// 16 bit RGB, 5:6:5.
enum {
  Black   = 0x0000,
  White   = 0xffff,
  Red     = 0xf800,
  Green   = 0x07e0,
  Blue    = 0x001f,
  Cyan    = 0x07ff,
  Magenta = 0xf81f,
  Yellow  = 0xffe0,
  Orange  = 0xfc00,
};

// Text justification relative to the current text area.
enum Justify { Left, Center, Right };

class Display {
public:
  // Pixels per text line. It matches the built-in font. A pixel buffer for a
  // full line of text, as wide as the screen, will allocated as a offscreen
  // render buffer.
  static constexpr uint16_t row_size = 60;

  // The baseline of the font.
  static constexpr uint16_t baseline = row_size * 3 / 4;

  constexpr Display(uint16_t width,
                    uint16_t height,
                    bool y_centered,
                    SPIClass *spi,
                    int8_t pin_cs,
                    int8_t pin_dc,
                    int8_t pin_reset) :
    _pin{.cs{pin_cs}, .dc{pin_dc}, .reset{pin_reset}},
    _sercom{},
    _spi{spi},
    _hardware{.width{width}, .height{height}, .y_centered{y_centered}},
    _buffer{} {}

  constexpr Display(uint16_t width,
                    uint16_t height,
                    bool y_centered,
                    uint8_t pin_data,
                    uint8_t pin_clock,
                    SERCOM *sercom,
                    SercomSpiTXPad pad_tx,
                    EPioType pin_func,
                    int8_t pin_cs,
                    int8_t pin_dc,
                    int8_t pin_reset) :
    _pin{.cs{pin_cs}, .dc{pin_dc}, .reset{pin_reset}},
    _sercom{.pin{.data{pin_data}, .clock{pin_clock}}, .sercom{sercom}, .pad_tx{pad_tx}, .pin_func{pin_func}},
    _spi{},
    _hardware{.width{width}, .height{height}, .y_centered{y_centered}},
    _buffer{} {}

  void begin();
  void reset(uint16_t orientation, uint16_t color);
  void loop();
  void fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
  void fillScreen(uint16_t color) {
    fillRectangle(0, 0, _pixels.width, _pixels.height, color);
  }

  // Define the current area to draw text. The cursor is set to 0.
  void setArea(uint16_t x, uint8_t row, uint16_t width, Justify justify, uint16_t foreground, uint16_t background) {
    _area.x          = x;
    _area.row        = row;
    _area.width      = width;
    _area.justify    = justify;
    _area.foreground = foreground;
    _area.background = background;
    _area.cursor     = 0;
  }

  void setColor(uint16_t color) {
    _area.foreground = color;
  }

  // Draw a single character at the cursor position in the defined area. No text
  // handling, always the default font size, left-justified.
  void drawChar(char c);

  // Print a line of text into the defined area.
  //
  // If the display is idle the text will be rendered to an offscreen buffer,
  // and the copying of the pixels offloaded to the DMA engine. In this case,
  // this call returns before the display is updated. It the frequency of updates
  // is lower than the time needed to transmit the pixels to the display, there
  // will be no waiting for I/O.
  //
  // If the display is busy, the call will block until the currently running job
  // has finished, and this job can be offloaded.
  void print(const char s[] = NULL);
  void print(float f, uint8_t digits = 2);

protected:
  struct {
    struct {
      const uint8_t data;
      const uint8_t clock;
    } pin;
    SERCOM *sercom;
    const SercomSpiTXPad pad_tx;
    const EPioType pin_func;
  } _sercom{};

  SPIClass *_spi{};

  const struct {
    int8_t cs;
    int8_t dc;
    int8_t reset;
  } _pin;

  // Physical properties of the display hardware.
  const struct {
    uint16_t width;
    uint16_t height;
    bool y_centered;
  } _hardware;

  // Visible pixels after reset/rotation.
  struct {
    uint16_t width;
    uint16_t height;
    uint16_t x_start;
    uint16_t y_start;
  } _pixels{};

  // Current text area.
  struct {
    Justify justify;
    uint16_t x;
    uint8_t row;
    uint16_t width;
    uint16_t foreground;
    uint16_t background;
    uint16_t cursor;
  } _area{};

  // SPI functions called by the hardware implementation.
  void prepareWrite();
  void finishWrite();
  void writeCommand(uint8_t commandByte, const uint8_t *data = NULL, uint8_t len = 0);

  // Provided by the hardware implementation.
  virtual void writeReset()                                                            = 0;
  virtual void writeSetOrientation(uint16_t angle)                                     = 0;
  virtual void writeSetWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height) = 0;

private:
  bool _busy{};
  uint16_t *_buffer;

  void write(const void *buffer, uint16_t len);
  void writeFillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
  void initializeBuffer();
  void flushBuffer();
};

// Sitronix ST7789V, 240 x 320 pixel graphics controller. Connected displays with fewer
// pixels on the x-axis use the pixel around the center, on the y-axis some use the pixels
// around the center, others start at 0.
class ST7789 : public Display {
public:
  constexpr ST7789(uint16_t width,
                   uint16_t height,
                   bool y_centered,
                   SPIClass *spi,
                   int8_t pinCS,
                   int8_t pinDC,
                   int8_t pinReset) :
    Display(width, height, y_centered, spi, pinCS, pinDC, pinReset) {}

  constexpr ST7789(uint16_t width,
                   uint16_t height,
                   bool y_centered,
                   uint8_t pin_data,
                   uint8_t pin_clock,
                   SERCOM *sercom,
                   SercomSpiTXPad pad_tx,
                   EPioType pin_func,
                   int8_t pin_cs,
                   int8_t pin_dc,
                   int8_t pin_reset) :
    Display(width, height, y_centered, pin_data, pin_clock, sercom, pad_tx, pin_func, pin_cs, pin_dc, pin_reset) {}

  void enable(boolean on);
  void sleep(boolean on);

private:
  void writeReset() override;
  void writeSetOrientation(uint16_t angle) override;
  void writeSetWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height) override;
};
};
