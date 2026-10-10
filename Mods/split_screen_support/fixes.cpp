/*
 * What NFSMWExtraOptions' ExperimentalSplitScreenFix changes, found there and re-read in the disassembly. Its three byte
 * patches, here one switch each (the entry in the Quick Race menu is menu_entry.cpp). FEDatabase (the pointer at 0x91CF90)
 * holds the player count in its first byte (1 or 2), the joystick port of each player at +0x0C and +0x0D, and game mode
 * flags at +0x12C; bit 2 means Quick Race, so "Quick Race with 2 players" is a split screen game.
 *
 *  shared_device   uiQRPressStart::NotificationMessage (0x7A3F20): when player 2 presses Start the game refuses the device
 *                  that player 1 has already (it compares with the port at +0x0C). The PC version has one input device,
 *                  port 0, for the keyboard and every controller, so every press is port 0 and player 2 could never get
 *                  ready. With the port at +0x0D, player 2's own (still unset), nothing is refused. (Who pressed is found
 *                  out through XInput instead, players.cpp.)
 *  loading_screen  TrackLoader::LoadHandler (0x666D00): the loading screen is picked from two lists, the second for split
 *                  screen; a jump is made unconditional so that the first is used (split screen's crashes).
 *  view_mode       MaybeChangeViewMode (0x6CFAD0) sets the views up for the view modes 0 to 2 (none, one view, one view
 *                  with the rear view mirror) and does nothing for 3, two views, which the PC renderer was not made for:
 *                  after the loading screen (mode 0, every view off) the views stay off, a black screen. Mode 3 is set up
 *                  as mode 1, player 1's view on the whole screen, and split_view.cpp draws the two halves from it. (The
 *                  ExtraOptions patch instead kept mode 0 from switching the views off, in every game.)
 */
#include "fixes.h"

#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::fixes {

namespace {

void report(const char *what, bool done, bool wanted) {
    if (!wanted) g_log.line("fix %s: off in the ini", what);
    else if (done) g_log.line("fix %s: installed", what);
    else g_log.line("fix %s: the game's code was changed by another mod (NFSMWExtraOptions' ExperimentalSplitScreenFix?); not installed", what);
}

void shared_device() {
    static const unsigned char kExpected[4] = {0x0F, 0xBE, 0x79, 0x0C};  // movsx edi, byte ptr [ecx+0x0C]
    static const unsigned char kPatched[4] = {0x0F, 0xBE, 0x79, 0x0D};   // movsx edi, byte ptr [ecx+0x0D]
    const bool wanted = ini_flag("fixes", "shared_device");
    report("shared_device", wanted && nfsmw::patch_bytes(0x7A3F65, kExpected, kPatched, sizeof(kExpected)), wanted);
}

void loading_screen() {
    static const unsigned char kExpected[2] = {0x75, 0x07};  // jne 0x666EBC
    static const unsigned char kPatched[2] = {0xEB, 0x07};   // jmp 0x666EBC
    const bool wanted = ini_flag("fixes", "loading_screen");
    report("loading_screen", wanted && nfsmw::patch_bytes(0x666EB3, kExpected, kPatched, sizeof(kExpected)), wanted);
}

// 0x6CFBFD: mov edi,[0x982BE8]; je 0x6CFC61 (the flags are those of "sub ebp,0": ebp is the view mode, 0 = switch every
// view off; then "dec ebp" twice picks modes 1 and 2). Replaced by a jump to a stub that does the same with mode 3 made 1.
void view_mode() {
    static const unsigned char kExpected[8] = {0x8B, 0x3D, 0xE8, 0x2B, 0x98, 0x00, 0x74, 0x5C};
    constexpr uintptr_t kSite = 0x6CFBFD, kNextMode = 0x6CFC05, kAllViewsOff = 0x6CFC61;
    const bool wanted = ini_flag("fixes", "view_mode");
    if (!wanted) return report("view_mode", false, false);
    nfsmw::Stub stub;
    stub.emit({0x8B, 0x3D, 0xE8, 0x2B, 0x98, 0x00});  // mov edi,[0x982BE8]   what the game had here
    stub.emit({0x83, 0xFD, 0x03});                    // cmp ebp,3   two views
    const size_t not_two = stub.jump_short(0x75);     // jne
    stub.emit({0xBD, 0x01, 0x00, 0x00, 0x00});        // mov ebp,1   one view
    stub.resolve_short(not_two);
    stub.emit({0x85, 0xED});                          // test ebp,ebp
    stub.emit({0x0F});
    stub.emit_rel32(0x84, kAllViewsOff);              // je   mode 0: every view off
    stub.emit_rel32(0xE9, kNextMode);                 // jmp  the modes 1 and 2
    stub.done();
    report("view_mode", nfsmw::jump_to_stub(kSite, kExpected, sizeof(kExpected), stub), true);
}

}  // namespace

void install() {
    shared_device();
    loading_screen();
    view_mode();
}

}  // namespace split_screen_support::fixes
