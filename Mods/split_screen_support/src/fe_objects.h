// The game's FEng objects (the HUD and menus) as the PC version lays them out in memory (the decomp's layouts do not match: see agent_docs/split-screen.md).
//   FEObject: next +4, name hash +0x10, type +0x18 (1 image, 2 string, 5 group, 12 multi image), flags +0x1C (bit 0: invisible), data +0x2C, data size +0x30,
//   current script +0x54, render cache +0x58; FEObjData: colour +0 (b, g, r, a as ints), position +0x1C, size +0x38; a group's first child +0x64, its count +0x60.
//   FEPackage: name +0xC, first object +0x4C.
#pragma once

#include <cstddef>
#include <cstdint>

namespace split_screen_support::fe {

template <typename T>
T &field(void *base, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

constexpr int kTypeImage = 1, kTypeString = 2, kTypeGroup = 5, kTypeMultiImage = 12;
constexpr unsigned kFlagInvisible = 1u;
constexpr unsigned kFlagDirty = (1u << 25) | (1u << 24);  // FF_Dirty | FF_DirtyTransform: the render cache is made again

inline void *next_of(void *object) { return field<void *>(object, 4); }
inline unsigned hash_of(void *object) { return field<unsigned>(object, 0x10); }
inline int type_of(void *object) { return field<int>(object, 0x18); }
inline bool is_group(void *object) { return type_of(object) == kTypeGroup; }
inline void *first_child(void *group) { return field<void *>(group, 0x64); }
inline void *first_object(void *package) { return field<void *>(package, 0x4C); }
inline const char *package_name(void *package) { return field<const char *>(package, 0xC); }

inline float &position_x(void *object) { return field<float>(field<void *>(object, 0x2C), 0x1C); }
inline float &position_y(void *object) { return field<float>(field<void *>(object, 0x2C), 0x20); }

// A package by name ("HUD_Player1.fng") of those FEng has loaded, or null.
inline void *find_package(const char *name) {
    constexpr uintptr_t kFengInstance = 0x91CADC, kFindPackage = 0x516AF0;  // cFEng::mInstance; cFEng::FindPackage
    void *feng = *reinterpret_cast<void **>(kFengInstance);
    return feng ? reinterpret_cast<void *(__fastcall *)(void *, void *, const char *)>(kFindPackage)(feng, nullptr, name) : nullptr;
}

}  // namespace split_screen_support::fe
