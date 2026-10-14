#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct BleReport {
  uint8_t id;
  size_t length;
  uint8_t data[8];
};

inline bool isHidpressDevice(uint16_t vid, uint16_t pid) {
  return vid == 0xd812 && (pid == 0x0020 || pid == 0x0021);
}

// Known QMK layouts: boot keyboard on interface 0, shared reports on 2.
inline bool routeReport(uint8_t interfaceNumber, const uint8_t *data,
                        size_t length, BleReport &out) {
  out = {};
  if (!data) return false;
  if (interfaceNumber == 0 && length == 8) {
    out.id = 1;
    out.length = 8;
  } else if (interfaceNumber == 2 && length == 6 && data[0] == 2) {
    out.id = 2;
    out.length = 5;
    ++data;
  } else if (interfaceNumber == 2 && length == 3 && data[0] == 4) {
    out.id = 4;
    out.length = 2;
    ++data;
  } else {
    return false;
  }
  memcpy(out.data, data, out.length);
  return true;
}
