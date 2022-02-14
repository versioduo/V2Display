// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#include "V2Display.h"
#include "font/Font.h"
#include <limits.h>
#include <wiring_private.h>

void V2Display::Display::begin() {
  _buffer = (uint16_t *)malloc(_hardware.width * row_size * sizeof(uint16_t));

  // Build SPI bus from SERCOM.
  //
  // SPIClass.begin() applies the board config to all given pins, which might not
  // match our configuration. Just pass the same pin to all of them, to make sure
  // we do not touch anything else. Our pin will be switched to the SERCOM after
  // begin().
  if (!_spi)
    _spi = new SPIClass(_sercom.sercom,
                        _sercom.pin.data,
                        _sercom.pin.data,
                        _sercom.pin.data,
                        _sercom.pad_tx,
                        SERCOM_RX_PAD_3);

  // Use faster clock, the transaction requests 60 Mhz.
  _spi->setClockSource(SERCOM_CLOCK_SOURCE_FCPU);

  _spi->begin();

  if (_sercom.sercom) {
    pinPeripheral(_sercom.pin.data, _sercom.pin_func);
    pinPeripheral(_sercom.pin.clock, _sercom.pin_func);
  }
}

void V2Display::Display::reset(uint16_t orientation, uint16_t color) {
  pinMode(_pin.cs, OUTPUT);
  digitalWrite(_pin.cs, LOW);

  pinMode(_pin.dc, OUTPUT);
  digitalWrite(_pin.dc, HIGH);

  pinMode(_pin.reset, OUTPUT);
  digitalWrite(_pin.reset, LOW);
  delay(1);
  digitalWrite(_pin.reset, HIGH);
  delay(5);

  _busy = false;
  prepareWrite();
  writeReset();
  writeSetOrientation(orientation);
  writeFillRectangle(0, 0, _pixels.width, _pixels.height, color);
  finishWrite();
}

void V2Display::Display::loop() {
  if (!_busy)
    return;

  if (_spi->isBusy())
    return;

  finishWrite();
  _busy = false;
}

void V2Display::Display::prepareWrite() {
  while (_busy) {
    yield();
    loop();
  }

  // Needs SPIClass::setClockSource(SERCOM_CLOCK_SOURCE_FCPU) to work.
  _spi->beginTransaction(SPISettings(60000000, MSBFIRST, SPI_MODE2));
  digitalWrite(_pin.cs, LOW);
}

void V2Display::Display::write(const void *buffer, uint16_t len) {
  while (_spi->isBusy())
    yield();

  //_spi->transfer(buffer, NULL, len, false);
  for (uint16_t i = 0; i < len; i++)
    _spi->transfer(((uint8_t *)buffer)[i]);
}

void V2Display::Display::finishWrite() {
  while (_spi->isBusy())
    yield();

  _spi->endTransaction();
  digitalWrite(_pin.cs, HIGH);
}

void V2Display::Display::writeCommand(uint8_t command, const uint8_t *data, uint8_t len) {
  while (_spi->isBusy())
    yield();

  digitalWrite(_pin.dc, LOW);
  _spi->transfer(command);
  digitalWrite(_pin.dc, HIGH);

  for (uint8_t i = 0; i < len; i++)
    _spi->transfer(data[i]);
}

// 240 * 240 * 16bit = 921600 bits
// 921600 bits / 60Mhz = 15.36 ms
void V2Display::Display::writeFillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
  writeSetWindow(x, y, width, height);

  // Write rows of pixels. Return when the last row is offloaded to the DMA engine.
  uint32_t n_pixels = width * height;
  uint32_t len      = min(n_pixels, _hardware.width * row_size);
  for (uint16_t i = 0; i < len; i++)
    _buffer[i] = __builtin_bswap16(color);

  while (n_pixels > 0) {
    const uint16_t count = min(n_pixels, len);
    write(_buffer, count * sizeof(uint16_t));
    n_pixels -= count;
  }
}

void V2Display::Display::fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
  while (_busy) {
    yield();
    loop();
  }

  prepareWrite();
  writeFillRectangle(x, y, width, height, color);
  _busy = true;
}

// Initialize offscreen buffer with background color.
void V2Display::Display::initializeBuffer() {
  for (uint16_t y = 0; y < row_size; y++)
    for (uint16_t x = 0; x < _area.width; x++)
      _buffer[(y * _area.width) + x] = __builtin_bswap16(_area.background);
}

// Offload the writing of the buffer to the DMA engine.
void V2Display::Display::flushBuffer() {
  prepareWrite();
  writeSetWindow(_area.x, _area.row * row_size, _area.width, row_size);
  write(_buffer, _area.width * row_size * sizeof(uint16_t));
  _busy = true;
}

static uint16_t renderChar(uint16_t *buffer,
                           const Font *font,
                           uint8_t width,
                           uint16_t x,
                           uint16_t y,
                           uint8_t c,
                           uint16_t color) {
  const Font::Glyph *glyph = font->getGlyph(c);
  uint16_t o               = glyph->offset;
  uint8_t map              = 0;
  uint8_t bit              = 0;
  for (uint8_t iy = 0; iy < glyph->height; iy++) {
    for (uint8_t ix = 0; ix < glyph->width; ix++) {
      if (!(bit++ & 0x07))
        map = font->bitmaps[o++];

      if (map & 0x80) {
        const uint16_t bx         = x + glyph->xStart + ix;
        const uint16_t by         = y + glyph->yStart + iy;
        buffer[(width * by) + bx] = __builtin_bswap16(color);
      }
      map <<= 1;
    }
  }

  return glyph->advance;
}

void V2Display::Display::drawChar(char c) {
  while (_busy) {
    yield();
    loop();
  }

  if (_area.cursor == 0)
    initializeBuffer();

  _area.cursor += renderChar(_buffer, &fontDefault, _area.width, _area.cursor, baseline, c, _area.foreground);
}

// Calculate the width of the printed string.
static uint16_t getTextWidth(const char *s, uint8_t len, const Font *font, char text[32], uint8_t &textLen) {
  uint16_t width = 0;
  bool replaced{};
  textLen = 0;
  for (uint8_t i = 0; i < len; i++) {
    char c = s[i];
    if (c == 0)
      break;

    if (c < 20)
      continue;

    // Print a single '#' for a stream of UTF-8 characters.
    if (c > 0x7f) {
      if (replaced)
        continue;

      c        = '#';
      replaced = true;
      continue;

    } else
      replaced = false;

    text[textLen++] = c;
    width += font->getGlyph(c)->advance;
  }

  return width;
}

// 135 * 60 * 16bit = 129600 bits
// 129600 bits / 60Mhz = 2.16 ms
void V2Display::Display::print(const char s[]) {
  while (_busy) {
    yield();
    loop();
  }

  if (!s) {
    // Do not clear the buffer if drawChar() rendered characters.
    if (_area.cursor == 0)
      initializeBuffer();

    flushBuffer();
    return;
  }

  uint8_t len = strlen(s);
  if (len == 0)
    return;

  if (len > 32)
    len = 32;

  // Ignore trailing whitespace.
  while (s[len - 1] == ' ')
    len--;

  // Calculate the width of the printed string.
  const Font *font = &fontDefault;
  char text[32];
  uint8_t textLen;
  uint16_t textWidth = getTextWidth(s, len, &fontDefault, text, textLen);

  // Use the condensed font if the text does not fit into the area.
  if (textWidth > _area.width) {
    font      = &fontCondensed;
    textWidth = getTextWidth(s, len, font, text, textLen);
  }

  // Use the smaller font if the text does not fit into the area.
  if (textWidth > _area.width) {
    font      = &fontCondensedSmall;
    textWidth = getTextWidth(s, len, font, text, textLen);
  }

  if (textWidth > _area.width)
    textWidth = _area.width;

  switch (_area.justify) {
    case Left:
      _area.cursor = 0;
      break;

    case Center:
      _area.cursor = (_area.width - textWidth) / 2;
      break;

    case Right:
      _area.cursor = _area.width - textWidth;
      break;
  }

  initializeBuffer();

  // Render text.
  for (uint8_t i = 0; i < textLen; i++) {
    const uint16_t advance = font->getGlyph(text[i])->advance;
    if (_area.cursor + advance > _area.width)
      break;

    renderChar(_buffer, font, _area.width, _area.cursor, baseline, text[i], _area.foreground);
    _area.cursor += advance;
  }

  _area.cursor = 0;
  flushBuffer();
}

void V2Display::Display::print(float f, uint8_t digits) {
  char s[32];
  snprintf(s, sizeof(s), "%.*f", digits, f);
  print(s);
}
