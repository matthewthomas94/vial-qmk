#include <assert.h>
#include <stdio.h>
#include "../s3_zero_usb_bridge/host_slots.h"

namespace {
const uint8_t publicAddress[6] = {0x34, 0x85, 0x18, 0x01, 0x02, 0x03};
const uint8_t macRpa[6] = {0x5a, 1, 1, 1, 1, 1}, macId[6] = {0xc8, 1, 1, 1, 1, 1};
const uint8_t macRpa2[6] = {0x6b, 2, 2, 2, 2, 2};
const uint8_t pc[6] = {0x10, 3, 3, 3, 3, 3}, phone[6] = {0x20, 4, 4, 4, 4, 4};

bool same(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }
}  // namespace

int main() {
  // Slot 1 at generation 0 keeps the public address; the rest are distinct
  // static random addresses that keep the low bytes.
  uint8_t seen[slotCount * 16][6];
  int count = 0;
  for (int slot = 0; slot < slotCount; ++slot) {
    for (int generation = 0; generation < 16; ++generation) {
      uint8_t address[6];
      const bool random = slotAddress(publicAddress, slot, generation, address);
      assert(random == !(slot == 0 && generation == 0));
      if (random) {
        assert((address[0] & 0xc0) == 0xc0);
        assert((address[0] & 0x3f) != 0 && (address[0] & 0x3f) != 0x3f);
      } else {
        assert(same(address, publicAddress));
      }
      assert(!memcmp(address + 1, publicAddress + 1, 5));
      for (int i = 0; i < count; ++i) assert(!same(seen[i], address));
      memcpy(seen[count++], address, 6);
    }
  }

  // Emptying a slot moves it to a new generation, never back to 0.
  HostSlots hosts{};
  uint8_t generation = 0;
  for (int i = 0; i < 40; ++i) {
    clearSlot(hosts, 0);
    assert(hosts.generation[0] != 0 && hosts.generation[0] != generation);
    generation = hosts.generation[0];
  }

  // A host owns its slot; the same host after pairing again (new bond
  // address, same identity) still does.
  hosts = {};
  assignSlot(hosts, 0, macRpa, macId);
  assignSlot(hosts, 1, pc, pc);
  assert(decideAuth(hosts, 0, macRpa, macId) == AuthAction::Accept);
  assert(decideAuth(hosts, 0, macRpa2, macId) == AuthAction::Accept);
  assert(decideAuth(hosts, 1, pc, pc) == AuthAction::Accept);

  // A new host lands in the empty slot it paired on.
  assert(decideAuth(hosts, 2, phone, phone) == AuthAction::Accept);

  // A saved host never moves to another slot, empty or not, and keeps its
  // bond (e.g. the Mac connecting as the address changes to an empty slot).
  assert(decideAuth(hosts, 2, macRpa, macId) == AuthAction::Reject);
  assert(decideAuth(hosts, 2, pc, pc) == AuthAction::Reject);
  assert(decideAuth(hosts, 0, pc, pc) == AuthAction::Reject);

  // An occupied slot drops bonds no slot holds.
  assert(decideAuth(hosts, 1, phone, phone) == AuthAction::RejectAndForget);

  // Re-pairing slot 2 (BOOT 5 s): its host is forgotten and a new one fits.
  const uint8_t before = hosts.generation[1];
  clearSlot(hosts, 1);
  assert(!hosts.used[1] && hosts.generation[1] != before);
  assert(slotOwning(hosts, pc, pc) == -1);
  assert(decideAuth(hosts, 1, phone, phone) == AuthAction::Accept);
  assignSlot(hosts, 1, phone, phone);
  assert(slotOwning(hosts, phone, phone) == 1);

  // Roaming after boot visits saved slots only.
  hosts = {};
  assert(nextRoamingSlot(hosts, 0) == 0);
  assignSlot(hosts, 0, macRpa, macId);
  assert(nextRoamingSlot(hosts, 0) == 0);
  assignSlot(hosts, 2, phone, phone);
  assert(nextRoamingSlot(hosts, 0) == 2);
  assert(nextRoamingSlot(hosts, 2) == 0);
  hosts.used[0] = false;
  assert(nextRoamingSlot(hosts, 0) == 2);
  assert(nextRoamingSlot(hosts, 2) == 2);
  puts("Host slot tests passed.");
}
