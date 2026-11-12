// The racers of the race: GRaceStatus (the pointer at 0x91E000) keeps the count at +0x1958 and a GRacerInfo of 0x194 bytes each from +0x18, the players first and then the
// opponents. A racer's simable is found by the handle at the start of its GRacerInfo in the table at 0x92FEB4.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace split_screen_support::racers {

constexpr int kMax = 16;  // the game's race state keeps the racers in tables of 16 (GRaceStatus: the GRacerInfos, then the count; lap times, check times...): more needs them moved
constexpr unsigned kInfoSize = 0x194;
// GRacerInfo (the PC layout)
constexpr unsigned kKnockedOut = 0x1C, kTotalled = 0x1D, kEngineBlown = 0x1E, kFinished = 0x21;  // bytes

// Out of the race: knocked out, totalled or with a blown engine.
inline bool out(const unsigned char *racer) { return racer[kKnockedOut] || racer[kTotalled] || racer[kEngineBlown]; }

// [options] max_racers: how many cars a race has at most, players and opponents (4 up to kMax). The memory and the slots for customized paint and engine sounds that more
// racers need are made at start-up from this one number. `ini` is the path of the ini (g_ini; the parts that start before it is known read the file themselves).
inline int max_racers(const char *ini) { return std::clamp(static_cast<int>(GetPrivateProfileIntA("options", "max_racers", kMax, ini)), 4, kMax); }

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

// The IVehicle of a simable (its interface list answers for the vehicle's handle), null when it has none.
inline void *vehicle(void *racer_simable) {
    constexpr uintptr_t kFind = 0x5D59F0, kVehicleInterface = 0x4040E0;
    if (!racer_simable) return nullptr;
    void *interfaces = *reinterpret_cast<void **>(static_cast<char *>(racer_simable) + 4);
    return reinterpret_cast<void *(__thiscall *)(void *, void *)>(kFind)(interfaces, reinterpret_cast<void *>(kVehicleInterface));
}

// IVehicle::Activate / Deactivate: in the world, or out of it (not drawn, not simulated; the car and its parts stay). Activate does nothing to a car that is in.
constexpr unsigned kVehicleActivateSlot = 0x80 / 4, kVehicleDeactivateSlot = 0x84 / 4;
inline void call_vehicle(void *vehicle, unsigned slot) {
    reinterpret_cast<void(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(vehicle))[slot])(vehicle, nullptr);
}

// The racer of a simable (-1 when it is none of the race's).
inline int index_of_simable(void *racer_simable) {
    constexpr uintptr_t kGetRacerInfo = 0x5E8B20;
    unsigned char *s = status();
    if (!s || !racer_simable) return -1;
    return index_of(reinterpret_cast<unsigned char *(__fastcall *)(void *, void *, void *)>(kGetRacerInfo)(s, nullptr, racer_simable));
}

// The racer of a player's pointer (an IPlayer), -1 when it is none.
inline int of_player(void *player) {
    constexpr unsigned kPlayerGetSimable = 1;  // IPlayer virtual table slot
    if (!player) return -1;
    return index_of_simable(reinterpret_cast<void *(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(player))[kPlayerGetSimable])(player, nullptr));
}

}  // namespace split_screen_support::racers
