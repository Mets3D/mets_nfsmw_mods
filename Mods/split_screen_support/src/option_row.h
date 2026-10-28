// The rows this mod adds to the race options of a split screen race (UIQRTrackOptions, Track_Options.fng): the way nfsmw_custom_menus builds the rows of its pages. A row is an
// FEToggleWidget (constructor 0x589300, 0x5C bytes) whose virtual table is PODriveCam's (the table those rows use) with Act and Draw replaced, handed to
// UIWidgetMenu::AddToggleOption (0x588570). The texts of a row are language strings registered through nfsmw_custom_menus.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace split_screen_support::option_row {

constexpr uintptr_t kNew = 0x652AD0, kAddToggleOption = 0x588570, kToggleWidgetCtor = 0x589300, kToggleDeletingDestructor = 0x589370, kRowVtable = 0x89C048,
                    kSetLanguageHash = 0x515C00, kRefreshWidgets = 0x588D20;
constexpr unsigned kWidgetSize = 0xB0;  // the largest widget of the Options screens is 0x9D bytes
constexpr unsigned kChanged = 0x2A, kTitle = 0x2C, kData = 0x30;  // FEToggleWidget: changed flag, label and value strings
constexpr unsigned kPadLeft = 0x9120409E, kPadRight = 0xB5971BF1;
constexpr int kBlinkSlot = 14, kDrawSlot = 3, kSlots = 15;

using ActFn = void(__fastcall *)(void *, void *, const char *, unsigned);  // FEToggleWidget::Act(const char *package, uint32 pad message)
using DrawFn = void(__fastcall *)(void *, void *);                         // FEToggleWidget::Draw

template <typename T>
T &field(void *object, unsigned offset) { return *reinterpret_cast<T *>(static_cast<char *>(object) + offset); }

// Fills a virtual table of kSlots entries: PODriveCam's with the destructor, Act (slot 1) and Draw (slot 3) of the row.
inline void make_vtable(void **out, ActFn act, DrawFn draw) {
    void **source = reinterpret_cast<void **>(kRowVtable);
    for (int i = 0; i < kSlots; ++i) out[i] = source[i];
    out[0] = reinterpret_cast<void *>(kToggleDeletingDestructor);
    out[1] = reinterpret_cast<void *>(act);
    out[kDrawSlot] = reinterpret_cast<void *>(draw);
}

// Makes the row and adds it to the screen; the widget (null when it could not be made).
inline void *add(void *screen, void **vtable) {
    void *widget = reinterpret_cast<void *(*)(size_t)>(kNew)(kWidgetSize);
    if (!widget) return nullptr;
    memset(widget, 0, kWidgetSize);
    reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kToggleWidgetCtor)(widget, nullptr, 1);
    field<void **>(widget, 0) = vtable;
    reinterpret_cast<unsigned(__fastcall *)(void *, void *, void *, int)>(kAddToggleOption)(screen, nullptr, widget, 1);
    return widget;
}

// Draws every row of the screen: until then the layout's placeholder texts show.
inline void refresh(void *screen) { reinterpret_cast<void(__fastcall *)(void *, void *)>(kRefreshWidgets)(screen, nullptr); }

// What a Draw does: the row's label and value are language strings.
inline void set_texts(void *self, unsigned title_hash, unsigned value_hash) {
    auto set = reinterpret_cast<void (*)(void *, unsigned)>(kSetLanguageHash);
    set(field<void *>(self, kTitle), title_hash);
    set(field<void *>(self, kData), value_hash);
}

// What an Act does once the value has changed: the arrow blinks for the pad message and the row is drawn again.
inline void value_changed(void *self, unsigned data) {
    field<unsigned char>(self, kChanged) = 1;
    void **vtable = field<void **>(self, 0);
    reinterpret_cast<void(__fastcall *)(void *, void *, unsigned)>(vtable[kBlinkSlot])(self, nullptr, data);
    reinterpret_cast<void(__fastcall *)(void *, void *)>(vtable[kDrawSlot])(self, nullptr);
}

}  // namespace split_screen_support::option_row
