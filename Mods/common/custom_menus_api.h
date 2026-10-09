// The Options menu framework's interface (nfsmw_custom_menus.asi). A mod that wants a page in Pause > Options exports
//
//     extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host);
//
// and calls host->register_page() from it. The framework calls that export, once, in every loaded plugin the first
// time the Options menu opens, so load order does not matter, and a mod still works (without its page) when the
// framework is missing. A page is a title, an icon and a list of rows. The framework builds the screen from the game's
// own Options layout (so it looks and feels native), with the Help hint and message box and Accept / Back / Defaults,
// and calls back here for everything about the values.
#pragma once

namespace nfsmw::custom_menus {

enum RowKind {
    kValueRow,           // "label    < value >": left / right call Page::change
    kCameraSelectorRow,  // the game's own Camera selector (it changes the player's camera view); the other rows
                         // are redrawn when it changes, so they can show the selected view's values
};

struct Row {
    RowKind kind;
    const char *label;    // title case; ignored for kCameraSelectorRow
    unsigned label_hash;  // nonzero: show this language string of the game instead of `label`
    const char *help;     // the Help message box (2 key / X button)
};

struct Page {
    const char *id;            // unique, e.g. "camera"
    const char *entry_text;    // the Options menu entry, title case
    const char *title;         // the screen title, upper case
    const char *help_title;    // the Help message box title, upper case
    unsigned icon;             // texture hash of the entry's and the screen's icon, if the game has it loaded...
    unsigned fallback_icon;    // ...else this one (a texture the game always has)
    const char *texture_pack;  // full path of a .tpk that holds `icon`, or null (loaded when the menu first opens)
    int row_count;
    const Row *rows;  // static
    // Each time the pause menu's Options opens: does the page apply (a race is running, a controller is present...)?
    bool (*available)();
    // The page was opened: remember the current values (what was last accepted).
    void (*opened)();
    // The row's value: returns a language hash of the game to show, or 0 and writes text to `out` ('\x01' = degree sign).
    unsigned (*value)(int row, char *out, unsigned out_size);
    // Left (-1) or right (+1) was pressed on the row; `fine` while Shift is held. Takes effect at once.
    void (*change)(int row, int direction, bool fine);
    bool (*changed)();   // do the values differ from when the page was opened? (Back then asks before discarding)
    void (*accept)();    // the page was left with changes: keep and save them
    void (*discard)();   // Back was confirmed: put the values from when the page opened back
    void (*defaults)();  // Defaults was confirmed: reset the page (the selected view, for the camera)
};

struct Host {
    void (*register_page)(const Page *page);  // the page must stay valid for the whole run
};

using ProvideFn = void (*)(const Host *);
constexpr const char *kProvideExport = "nfsmw_custom_menus_provide";

}  // namespace nfsmw::custom_menus
