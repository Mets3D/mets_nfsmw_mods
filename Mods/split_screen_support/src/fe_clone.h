// Copies of FEng objects (a marker of the minimap, an icon of the world map) made with the game's own FEObject::Clone, which needs help to make a usable one.
#pragma once

#include <cstring>

#include "fe_objects.h"
#include "split_screen_support.h"

namespace split_screen_support::fe {

// The game's Clone makes the object and its scripts but not its data (finish_clone does that), and only images, groups and multi
// images have no pointers in their data; scripts are needed because Clone reads the current one without a check.
inline bool can_clone(void *object, int depth = 0) {
    const int type = field<int>(object, 0x18);
    if (type != kTypeImage && type != kTypeGroup && type != kTypeMultiImage) {  // no pointers in their data
        g_log.line("fe: object %08x has type %d, cloning it is not safe", field<unsigned>(object, 0x10), type);
        return false;
    }
    if (!field<void *>(object, 0x54)) {
        g_log.line("fe: object %08x has no script, cloning it is not safe", field<unsigned>(object, 0x10));
        return false;
    }
    if (!is_group(object) || depth > 8) return true;
    for (void *c = first_child(object); c; c = next_of(c))
        if (!can_clone(c, depth + 1)) return false;
    return true;
}

// FEObject::Clone leaves the object's data zeroed (alpha 0: invisible) and a multi image's texture fields unset. The copy also has
// new GUIDs, may lack the name hash (packages keep only the hash) and must not inherit a render cache.
inline void finish_clone(void *original, void *copy, int depth = 0) {
    field<unsigned>(copy, 0x10) = field<unsigned>(original, 0x10);
    const unsigned size = field<unsigned>(original, 0x30);
    if (field<unsigned>(copy, 0x30) == size && field<void *>(original, 0x2C) && field<void *>(copy, 0x2C))
        memcpy(field<void *>(copy, 0x2C), field<void *>(original, 0x2C), size);
    if (field<int>(original, 0x18) == kTypeMultiImage) memcpy(static_cast<char *>(copy) + 0x60, static_cast<char *>(original) + 0x60, 0x18);  // textures, texture flags
    field<void *>(copy, 0x58) = nullptr;
    field<unsigned>(copy, 0x1C) |= kFlagDirty;
    if (!is_group(original) || depth > 8) return;
    void *o = first_child(original), *c = first_child(copy);
    for (; o && c; o = next_of(o), c = next_of(c)) finish_clone(o, c, depth + 1);
}

// A usable copy of the object (not yet in any group or package), or null.
inline void *clone_object(void *object) {
    if (!can_clone(object)) return nullptr;
    using CloneFn = void *(__fastcall *)(void *, void *, bool);
    void *copy = reinterpret_cast<CloneFn>((*reinterpret_cast<void ***>(object))[1])(object, nullptr, false);
    if (copy) finish_clone(object, copy);
    return copy;
}

// The group that has `target` as a child (anywhere below `group`).
inline void *parent_of(void *group, void *target, int depth = 0) {
    if (depth > 8) return nullptr;
    for (void *c = first_child(group); c; c = next_of(c)) {
        if (c == target) return group;
        if (is_group(c))
            if (void *found = parent_of(c, target, depth + 1)) return found;
    }
    return nullptr;
}

// Puts the copy where the original is: in its group, or among the package's own objects when it is not in a group. False when the original was not found in the package.
inline bool add_beside(void *package, void *original, void *copy) {
    constexpr uintptr_t kGroupAddObject = 0x5B5420, kPackageAddObject = 0x50DB90;
    for (void *o = first_object(package); o; o = next_of(o)) {
        if (o == original) {
            reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(kPackageAddObject)(package, nullptr, copy);
            return true;
        }
        if (is_group(o))
            if (void *group = parent_of(o, original)) {
                reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(kGroupAddObject)(group, nullptr, copy);
                return true;
            }
    }
    return false;
}

}  // namespace split_screen_support::fe
