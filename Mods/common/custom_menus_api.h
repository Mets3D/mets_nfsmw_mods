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

// Where a page's rows go.
enum Placement {
    kNewEntry,       // a page of its own: an entry in the pause menu's Options, with an icon, a title and its own screen
    kPlayerOptions,  // the rows are added to the end of the game's own Player options (Options > Player, in the pause menu and
                     // the main menu); entry_text, title, help_title, icon, fallback_icon and texture_pack are not used, the
                     // Help button has no text for them, and the callbacks work as for a page, for those rows
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
    // Left (-1) or right (+1) was pressed on the row; `fine` while Shift or the controller's left shoulder button (LB, L1 on PlayStation pads) is held. Takes effect at once.
    void (*change)(int row, int direction, bool fine);
    bool (*changed)();   // do the values differ from when the page was opened? (Back then asks before discarding)
    void (*accept)();    // the page was left with changes: keep and save them
    void (*discard)();   // Back was confirmed: put the values from when the page opened back
    void (*defaults)();  // Defaults was confirmed: reset the page (the selected view, for the camera)
    Placement placement;  // where the rows go; leave it out for kNewEntry
};

// Rows added to Options > Controls (Pause > Options > Controls, and the main menu's). Each row edits one entry of the game's binding
// table (0x91F418; 0x34 bytes per entry: the name, then 4 slots {type, device, object}); the framework adds the rows, translates
// row -> entry for the screen, and leaves the entries' names, default and saved bindings and what they do to the mod that owns them.
// The row's title is the language string keyed by bStringHash(the entry's name in the table): register it with register_text.
struct ControlsRow {
    int entry;       // the binding table entry the row edits; it must be one the game's own screen does not list
    int after_row;   // the game's row it goes under (rows below move down by one), or -1 at the end of both lists. The game's rows are its
                     // entries 0-17 (keyboard list) and 0-18 (controller list)
};
// Called after the game wrote a binding's label ("Button 2"...) in the Controls screen, to rewrite it. `entry` is the table's entry.
using BindingLabelFn = void (*)(char *label, int column, int entry, bool primary);

struct Host {
    void (*register_page)(const Page *page);  // the page must stay valid for the whole run
    // Rows of the Controls screen (the array must stay valid for the whole run). Only effective when called from the provide
    // export, which runs before the Controls screen exists.
    void (*register_controls_rows)(const ControlsRow *rows, int count);
    // A language string: added to the game's table, or replacing the game's own text for that hash. `text` must stay valid.
    void (*register_text)(unsigned hash, const char *text);
    void (*set_binding_label)(BindingLabelFn fn);  // one mod may do this
    // Called each time the Controls screen refreshes its rows (it opens, and after every rebinding): where a mod notices that the
    // bindings of its entries changed. Any number of mods may add one.
    void (*add_controls_refresh)(void (*fn)());
};

using ProvideFn = void (*)(const Host *);
constexpr const char *kProvideExport = "nfsmw_custom_menus_provide";

}  // namespace nfsmw::custom_menus
