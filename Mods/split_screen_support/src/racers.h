// The racers of the race: GRaceStatus (the pointer at 0x91E000) keeps the count at +0x1958 and a GRacerInfo of 0x194 bytes each from +0x18, the players first and then the
// opponents. A racer's simable is found by the handle at the start of its GRacerInfo in the table at 0x92FEB4.
#pragma once

#include <cstdint>

namespace split_screen_support::racers {

constexpr int kMax = 16;  // GRaceStatus has room for 16 racers
constexpr unsigned kInfoSize = 0x194;
// GRacerInfo (the PC layout)
constexpr unsigned kKnockedOut = 0x1C, kFinished = 0x21;  // bytes

inline unsigned char *status() { return *reinterpret_cast<unsigned char **>(0x91E000); }

inline int count() {
    const unsigned char *s = status();
    return s ? *reinterpret_cast<const int *>(s + 0x1958) : 0;
}

inline unsigned char *info(int index) {
    unsigned char *s = status();
    return s && index >= 0 && index < count() ? s + 0x18 + index * kInfoSize : nullptr;
}

// The racer's index for a GRacerInfo of this race, -1 when it is not one.
inline int index_of(const unsigned char *racer) {
    const unsigned char *s = status();
    if (!s || !racer || racer < s + 0x18) return -1;
    const unsigned offset = static_cast<unsigned>(racer - (s + 0x18));
    return offset % kInfoSize == 0 && static_cast<int>(offset / kInfoSize) < count() ? static_cast<int>(offset / kInfoSize) : -1;
}

// The racer's simable (the ISimable the game's functions take), null when the racer has none.
inline void *simable(const unsigned char *racer) {
    constexpr uintptr_t kTable = 0x92FEB4, kTableCount = 0x92FEBC, kSearch = 0x5D51F0;
    const unsigned handle = racer ? *reinterpret_cast<const unsigned *>(racer) : 0;
    if (!handle) return nullptr;
    unsigned char *table = *reinterpret_cast<unsigned char **>(kTable);
    unsigned char *end = table + *reinterpret_cast<int *>(kTableCount) * 8;
    unsigned char *node = reinterpret_cast<unsigned char *(__cdecl *)(unsigned char *, unsigned char *, unsigned)>(kSearch)(table, end, handle);
    return node ? *reinterpret_cast<void **>(node + 4) : nullptr;
}

// The racer of a simable (-1 when it is none of the race's).
inline int index_of_simable(void *racer_simable) {
    constexpr uintptr_t kGetRacerInfo = 0x5E8B20;
    unsigned char *s = status();
    if (!s || !racer_simable) return -1;
    return index_of(reinterpret_cast<unsigned char *(__fastcall *)(void *, void *, void *)>(kGetRacerInfo)(s, nullptr, racer_simable));
}

}  // namespace split_screen_support::racers
