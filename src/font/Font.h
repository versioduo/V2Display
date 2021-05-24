#include <inttypes.h>

// DIN1451, ASCII characters 0x20 - 0x7e only.
// TrueType font DIN1451, © Peter Wiegel.

class Font {
public:
  struct Glyph {
    uint16_t offset;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    int8_t xStart;
    int8_t yStart;
  };

  const uint8_t *bitmaps;
  const Glyph *glyphs;

  const Glyph *getGlyph(uint8_t c) const {
    return &glyphs[c - 0x20];
  }
};

extern const Font fontDefault;
extern const Font fontCondensed;
extern const Font fontCondensedSmall;
