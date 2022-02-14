// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#include "V2Display.h"
#include <V2Base.h>
#include <limits.h>

enum {
  CMD_NOP        = 0x00,
  CMD_SWRESET    = 0x01,
  CMD_RDDID      = 0x04,
  CMD_RDDST      = 0x09,
  CMD_SLPIN      = 0x10,
  CMD_SLPOUT     = 0x11,
  CMD_PTLON      = 0x12,
  CMD_NORON      = 0x13,
  CMD_INVOFF     = 0x20,
  CMD_INVON      = 0x21,
  CMD_DISPOFF    = 0x28,
  CMD_DISPON     = 0x29,
  CMD_CASET      = 0x2a,
  CMD_RASET      = 0x2b,
  CMD_RAMWR      = 0x2c,
  CMD_RAMRD      = 0x2e,
  CMD_PTLAR      = 0x30,
  CMD_TEOFF      = 0x34,
  CMD_TEON       = 0x35,
  CMD_MADCTL     = 0x36,
  CMD_COLMOD     = 0x3a,
  CMD_MADCTL_MY  = 0x80,
  CMD_MADCTL_MX  = 0x40,
  CMD_MADCTL_MV  = 0x20,
  CMD_MADCTL_ML  = 0x10,
  CMD_MADCTL_RGB = 0x00,
  CMD_RDID1      = 0xda,
  CMD_RDID2      = 0xdb,
  CMD_RDID3      = 0xdc,
  CMD_RDID4      = 0xdd
};

void V2Display::ST7789::enable(boolean on) {
  prepareWrite();
  writeCommand(on ? CMD_DISPON : CMD_DISPOFF);
  finishWrite();
}

void V2Display::ST7789::sleep(boolean on) {
  prepareWrite();
  writeCommand(on ? CMD_SLPIN : CMD_SLPOUT);
  finishWrite();
}

void V2Display::ST7789::writeSetWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  uint8_t data[4];

  const uint16_t x_start = x + _pixels.x_start;
  data[0]                = x_start >> 8;
  data[1]                = x_start & 0xff;
  const uint16_t x_end   = x_start + width - 1;
  data[2]                = x_end >> 8;
  data[3]                = x_end & 0xff;
  writeCommand(CMD_CASET, data, 4);

  const uint16_t y_start = y + _pixels.y_start;
  data[0]                = y_start >> 8;
  data[1]                = y_start & 0xff;
  const uint16_t y_end   = y_start + height - 1;
  data[2]                = y_end >> 8;
  data[3]                = y_end & 0xff;
  writeCommand(CMD_RASET, data, 4);

  writeCommand(CMD_RAMWR);
}

void V2Display::ST7789::writeSetOrientation(uint16_t angle) {
  uint8_t command = 0;

  switch (angle) {
    case 0:
      command         = CMD_MADCTL_RGB;
      _pixels.width   = _hardware.width;
      _pixels.height  = _hardware.height;
      _pixels.x_start = (240 - _hardware.width) / 2;
      _pixels.y_start = _hardware.y_centered ? (320 - _hardware.height) / 2 : 0;
      break;

    case 90:
      // Exchange X/Y, mirror X.
      command         = CMD_MADCTL_MX | CMD_MADCTL_MV | CMD_MADCTL_RGB;
      _pixels.width   = _hardware.height;
      _pixels.height  = _hardware.width;
      _pixels.x_start = _hardware.y_centered ? (320 - _hardware.height) / 2 : 0;
      _pixels.y_start = ((240 - _hardware.width) + 1) / 2;
      break;

    case 180:
      // Mirror X and Y.
      command         = CMD_MADCTL_MX | CMD_MADCTL_MY | CMD_MADCTL_RGB;
      _pixels.width   = _hardware.width;
      _pixels.height  = _hardware.height;
      _pixels.x_start = ((240 - _hardware.width) + 1) / 2;
      _pixels.y_start = _hardware.y_centered ? (320 - _hardware.height) / 2 : 320 - _hardware.height;
      break;

    case 270:
      // Exchange X/Y, mirror Y.
      command         = CMD_MADCTL_MY | CMD_MADCTL_MV | CMD_MADCTL_RGB;
      _pixels.width   = _hardware.height;
      _pixels.height  = _hardware.width;
      _pixels.x_start = _hardware.y_centered ? (320 - _hardware.height) / 2 : 320 - _hardware.height;
      _pixels.y_start = (240 - _hardware.width) / 2;
      break;
  }

  writeCommand(CMD_MADCTL, &command, 1);
}

void V2Display::ST7789::writeReset() {
  static const struct {
    uint8_t cmd;
    uint8_t nArgs;
    uint8_t args[1];
    uint8_t delay;
  } commands[]{{.cmd{CMD_SWRESET}, .delay{5}},
               {.cmd{CMD_SLPOUT}},
               {.cmd{CMD_COLMOD}, .nArgs{1}, .args{0x55}}, // 16 bit pixel
               {.cmd{CMD_MADCTL}, .nArgs{1}, .args{0x08}}, // RGB order
               {.cmd{CMD_INVON}},                          // Display inversion
               {.cmd{CMD_NORON}},
               {.cmd{CMD_DISPON}}};
  for (uint8_t i = 0; i < V2Base::countof(commands); i++) {
    writeCommand(commands[i].cmd, commands[i].args, commands[i].nArgs);
    delay(commands[i].delay);
  }
}
