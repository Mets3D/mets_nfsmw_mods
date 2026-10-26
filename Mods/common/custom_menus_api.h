// The Options menu framework's interface (nfsmw_custom_menus.asi). A mod that wants a page in the Options menu exports
//
//     extern "C" __declspec(dllexport) void nfsmw_custom_menus_provide(const nfsmw::custom_menus::Host *host);
//
// and calls host->register_page() from it. The framework calls that export, once, in every loaded plugin the first
// time the Options menu, the pause menu or the main menu is built, so load order does not matter, and a mod still works (without its page) when the
// framework is missing. A page is a title, an icon and a list of rows. The framework builds the screen from the game's
// own Options layout (so it looks and feels native), with the Help hint and message box and Accept / Back / Defaults,
// and calls back here for everything about the values.
#pragma once

namespace nfsmw::custom_menus {

enum RowKind {
    kValueRow,           // "label    < value >": left / right call Page::change
    kSliderRow,          // as kValueRow, but drawn as the game's slider bar (Audio's volumes) filled to Page::fill; `value` is shown
                         // instead when `fill` is null or the layout has no slider bars
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
    kHidden,         // no entry in the Options menu: the page is opened by the mod, with Host::open_page (the Page's `available` is
                     // not asked); otherwise as kNewEntry
    kNewEntryEverywhere,  // as kNewEntry, and also an entry in the main menu's Options (before EA Trax and Credits). `available`
                          // is asked there too, so the page must work with no race loaded
    // Rows added to the end of another of the game's own Options screens (pause menu and main menu), as for kPlayerOptions: the same
    // fields are unused and the callbacks work the same. The screens have a limited number of row slots (the game's own rows use most of
    // them): when the added rows of all mods do not fit, the screen scrolls as a whole (the game's own rows leave at the top), and the added
    // rows keep their kind, except that a kCameraSelectorRow is left out. The Video screen has two modes, the
    // brightness one (kVideoOptions) and the PC's long list of display options (kAdvancedVideoOptions); rows added to it do not trigger the
    // game's "keep these display settings?" question, and with more than 3 rows in all, the brightness text and pictures (which do not
    // scroll) are hidden.
    kAudioOptions,
    kVideoOptions,         // the Video screen as it opens: the brightness and a few rows (the "Advanced" hint leads to the next)
    kGameplayOptions,
    kAdvancedVideoOptions,  // the Advanced Video screen: the long list of the PC's display options (the Video screen's other mode)
    // An entry in the icon strip of the pause menu itself (Resume, Restart, ..., Options) or of the main menu (Career, Quick Race, ..., Options),
    // before that menu's Options, which opens the page: a screen of its own, as for kNewEntry, and Back / Accept return to the menu that was left.
    // For things that are not settings, say tools for the current race. `entry_text` is the name shown above the strip, `icon` / `fallback_icon` /
    // `texture_pack` the picture (the Options icon by default), `available` is asked each time the menu is built. The pause menu's entries are
    // not in the screen after a race, nor in online races. The entries need free slots in the layout's icon strip: the framework adds them.
    kPauseMenuEntry,
    kMainMenuEntry,
    kPauseAndMainMenuEntry,
};

struct Page {
    const char *id;            // unique, e.g. "camera"
    const char *entry_text;    // the Options menu entry, title case
    const char *title;         // the screen title, upper case
    const char *help_title;    // the Help message box title, upper case
    unsigned icon;             // texture hash of the entry's and the screen's icon, if the game has it loaded...
    unsigned fallback_icon;    // ...else this one (a texture the game always has), or 0 for the Options icon (two cogs)
    const char *texture_pack;  // full path of a .tpk that holds `icon`, or null (loaded when the menu first opens)
    int row_count;
    const Row *rows;  // static
    // Each time the menu with the page's entry is built (the pause menu's Options; the main menu's too, for kNewEntryEverywhere; the pause
    // menu or main menu itself, for kPauseMenuEntry...): does the page apply (a race is running, a controller is present...)?
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
    float (*fill)(int row);  // kSliderRow: how full the bar is, 0..1 (left / right still call `change`); leave it out for no sliders
    // Greyed out rows: false for a row that has no effect now (it is drawn grey, the cursor skips it and left / right do nothing). Asked
    // whenever the rows are drawn, which includes when the Camera selector changes. Leave it out for rows that are always usable.
    bool (*row_enabled)(int row);
    // kSliderRow: what the setting is for is doing now, as a green bar under the slider, drawn to the length of the slider (0..1), asked
    // every frame while the page is open; negative (or leave it out) for no bar. For example how far a stick is pushed, under its dead zone.
    float (*live)(int row);
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
    // Translations of the texts the mod gives the framework (pages, rows, help, register_text): `folder` (the mod's own; the string is copied)
    // holds one UTF-8 file per language, named like the game's language (German.txt, French.txt, ...), with a line `English text =
    // Translation` per text. Call it before registering pages and texts. Texts without a translation stay English.
    void (*register_translations)(const char *folder);
    void (*set_binding_label)(BindingLabelFn fn);  // one mod may do this
    // Called each time the Controls screen refreshes its rows (it opens, and after every rebinding): where a mod notices that the
    // bindings of its entries changed. Any number of mods may add one.
    void (*add_controls_refresh)(void (*fn)());
    // Called right after the game reset the controller bindings to its defaults (the Controls screen's Defaults button, and loading
    // a profile saved with another kind of controller), which also clears the controller slots (2 and 3) of entries the game
    // does not know: put the default bindings of your entries back. Any number of mods may add one.
    void (*add_controller_defaults)(void (*fn)());
    // Opens the page `id` (any page with a screen of its own) from another screen, which the caller is leaving: it switches to the
    // page's screen. `in_game` says which set of the game's layouts to use: true for the pause menu's, false for the main menu's.
    // When the page is left (Back, with the game's own question about unsaved changes, or Accept), `on_exit` is called instead of the
    // game's own way out, and has to switch to the next screen itself. Valid after the provide export returned.
    void (*open_page)(const char *id, bool in_game, void (*on_exit)());
};

using ProvideFn = void (*)(const Host *);
constexpr const char *kProvideExport = "nfsmw_custom_menus_provide";

}  // namespace nfsmw::custom_menus
