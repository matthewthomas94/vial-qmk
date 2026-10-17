#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include "../s3_zero_usb_bridge/report_router.h"

int main() {
  assert(isHidpressDevice(0xd812, 0x0020));
  assert(isHidpressDevice(0xd812, 0x0021));
  assert(!isHidpressDevice(0x1234, 0x0021));
  assert(!isHidpressDevice(0xd812, 0xffff));
  BleReport out{};
  uint8_t keyboard[] = {8, 0, 0x1d, 0, 0, 0, 0, 0};
  assert(routeReport(0, keyboard, sizeof(keyboard), out));
  assert(out.id == 1 && out.length == 8 && !memcmp(out.data, keyboard, 8));
  uint8_t release[8] = {};
  assert(routeReport(0, release, sizeof(release), out));
  assert(!memcmp(out.data, release, 8));
  uint8_t mouse[] = {2, 1, 0xff, 0x7f, 0xfe, 1};
  assert(routeReport(2, mouse, sizeof(mouse), out));
  assert(out.id == 2 && out.length == 5 && !memcmp(out.data, mouse + 1, 5));
  for (uint8_t usage : {uint8_t(0xe9), uint8_t(0xea), uint8_t(0xe2), uint8_t(0)}) {
    uint8_t consumer[] = {4, usage, 0};
    assert(routeReport(2, consumer, sizeof(consumer), out));
    assert(out.id == 4 && out.length == 2 && out.data[0] == usage && out.data[1] == 0);
  }
  assert(!routeReport(1, keyboard, sizeof(keyboard), out));
  assert(!routeReport(2, keyboard, sizeof(keyboard), out));
  assert(!routeReport(0, keyboard, 7, out));
  assert(!routeReport(2, mouse, 5, out));
  assert(!routeReport(2, mouse, 7, out));
  assert(!routeReport(2, mouse, 0, out));
  assert(!routeReport(0, nullptr, 8, out));
  for (uint8_t id : {uint8_t(3), uint8_t(6), uint8_t(7), uint8_t(0xff)}) {
    uint8_t ignored[] = {id, 0, 0};
    assert(!routeReport(2, ignored, sizeof(ignored), out));
  }
  puts("Report routing tests passed.");
}
