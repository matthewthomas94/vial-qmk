#pragma once

#include <stdint.h>
#include <string.h>

// Host slot policy, kept free of ESP-IDF types so it can be tested on a Mac.
// Each slot advertises its own local BLE address, so a host only ever sees
// the slot it paired with.
constexpr int slotCount = 3;

struct HostSlots {
  bool used[slotCount];
  uint8_t bond[slotCount][6];      // Address Bluedroid keeps the bond under.
  uint8_t identity[slotCount][6];  // Host identity address (PID key), else bond.
  uint8_t generation[slotCount];   // Bumped whenever the slot is emptied.
};

// Local address for a slot. Slot 1 generation 0 keeps the public address so a
// pairing made before per-slot addresses survives; every other slot and
// generation is a static random address (top bits 11) derived from it.
// Returns true when the address is random.
inline bool slotAddress(const uint8_t publicAddress[6], int slot,
                        uint8_t generation, uint8_t out[6]) {
  memcpy(out, publicAddress, 6);
  if (slot == 0 && generation == 0) return false;
  out[0] = 0xc0 | (slot * 16 + generation % 16);
  return true;
}

// Empty a slot. The new generation gives it a new address, so the host that
// was forgotten never sees its old device advertising again. Generations
// cycle 1..15 and never return to 0 (slot 1's public address).
inline void clearSlot(HostSlots &hosts, int slot) {
  hosts.used[slot] = false;
  hosts.generation[slot] = hosts.generation[slot] % 15 + 1;
}

inline void assignSlot(HostSlots &hosts, int slot, const uint8_t bond[6],
                       const uint8_t identity[6]) {
  hosts.used[slot] = true;
  memcpy(hosts.bond[slot], bond, 6);
  memcpy(hosts.identity[slot], identity, 6);
}

inline int slotOwning(const HostSlots &hosts, const uint8_t bond[6],
                      const uint8_t identity[6]) {
  for (int i = 0; i < slotCount; ++i) {
    if (hosts.used[i] && (memcmp(hosts.bond[i], bond, 6) == 0 ||
                          memcmp(hosts.identity[i], identity, 6) == 0)) {
      return i;
    }
  }
  return -1;
}

enum class AuthAction { Accept, Reject, RejectAndForget };

// A host finished encrypting while `advertised` slot's address was in use.
inline AuthAction decideAuth(const HostSlots &hosts, int advertised,
                             const uint8_t bond[6], const uint8_t identity[6]) {
  const int owner = slotOwning(hosts, bond, identity);
  if (owner == advertised) return AuthAction::Accept;
  // A host keeps its one slot. It shows up on another slot only by
  // connecting as the address changes, or by pairing there on purpose;
  // refuse it and keep its bond.
  if (owner >= 0) return AuthAction::Reject;
  // An empty slot takes a new host. An occupied slot is only reached by
  // pairing on purpose with its address: drop that bond.
  if (!hosts.used[advertised]) return AuthAction::Accept;
  return AuthAction::RejectAndForget;
}

// While no slot is chosen (after boot), advertising moves through the saved
// slots so any saved host can reconnect. Returns `current` if none is saved.
inline int nextRoamingSlot(const HostSlots &hosts, int current) {
  for (int i = 1; i <= slotCount; ++i) {
    const int slot = (current + i) % slotCount;
    if (hosts.used[slot]) return slot;
  }
  return current;
}
