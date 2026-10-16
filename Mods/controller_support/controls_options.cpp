#include "controls_options.h"

#include "input.h"
#include "pad_sets.h"
#include "rumble.h"

namespace pad::controls_options {

namespace {

constexpr unsigned kHintMsg = 0x36D64627;  // the hint was pressed
constexpr unsigned kPadLeftShoulder = 0x5073EF13;  // PAD_LTRIGGER
constexpr unsigned kPadRightShoulder = 0xD9FEEC59;  // PAD_RTRIGGER: ends in the device switch, after the "keep the modified settings?" question
constexpr unsigned kDialogClosed = 0x34DC1BCF;     // the question was left with Back
constexpr unsigned kScreenTick = 0xC98356BA;
constexpr unsigned kHintText = 0x3DF9961B;   // "Rumble: %s"
constexpr unsigned kColumn = 0xE4;                 // 1 keyboard, 3 controller
constexpr unsigned kInGame = 0xE0;                 // the pause menu's Controls screen
constexpr uintptr_t kFEng = 0x91CADC, kQueuePackageSwitch = 0x525940;

const nfsmw::custom_menus::Host *g_host = nullptr;
bool g_pending = false;      // the hint was pressed: the next device switch opens the page
bool g_in_game = false;      // which Controls screen it was
int g_return_pad = -1;       // the controller to select when the Controls screen is back, or -1

bool controller_selected(void *screen) { return field<int>(screen, kColumn) == 3; }

// The page was left: back to the Controls screen. The controller is selected again on its first tick.
void back_to_controls() {
    log_line("Controls menu: leaving the Settings page, switching back to the Controls screen");
    using SwitchFn = void(__attribute__((thiscall)) *)(void *feng, const char *pkg, int arg, unsigned msg, bool flag);
    reinterpret_cast<SwitchFn>(kQueuePackageSwitch)(*reinterpret_cast<void **>(kFEng),
                                                    g_in_game ? "Pause_PC_Controller.fng" : "Options_PC_Controller.fng",
                                                    g_in_game ? 1 : 0, 0, false);
}

}  // namespace

void provide(const nfsmw::custom_menus::Host *host) {
    if (!host->open_page) return;
    g_host = host;
    host->register_text(kHintText, "Settings");  // the hint no longer says on / off (the hint shows 14 characters at most)
}

unsigned filter_message(void *screen, unsigned message) {
    if (message == kPadLeftShoulder || message == kPadRightShoulder) g_pending = false;  // a real device switch
    if (message == kScreenTick) {
        if (g_return_pad >= 0) {  // a new Controls screen: the controller we came from
            const int pad = g_return_pad;
            g_return_pad = -1;
            log_line("Controls menu: back from the Settings page, selecting controller %d again", pad + 1);
            pad_sets::show_device(screen, 3, pad);
        }
        return message;
    }
    if (message == kDialogClosed) g_pending = false;
    if (message != kHintMsg) return message;
    if (!g_host || !controller_selected(screen)) {
        log_line("Controls menu: Settings hint pressed, but %s: the game's toggle", !g_host ? "the page is not available" : "no controller is selected");
        return message;
    }
    const int slot = input::rumble_slot_of_joystick(pad_sets::menu_pad());
    if (slot < 0) {
        log_line("Controls menu: Settings hint pressed, but controller %d is not an XInput controller: the game's toggle", pad_sets::menu_pad() + 1);
        return message;
    }
    g_pending = true;
    rumble::set_edit_slot(slot);
    log_line("Controls menu: Settings hint pressed for controller %d (XInput controller %d); asking about unsaved changes first if any",
             pad_sets::menu_pad() + 1, slot + 1);
    return kPadRightShoulder;
}

bool on_device_switch(void *screen) {
    if (!g_pending) return false;
    g_pending = false;
    g_in_game = field<unsigned char>(screen, kInGame) != 0;
    g_return_pad = pad_sets::menu_pad();
    log_line("Controls menu: opening the Settings page of controller %d (%s layout)", g_return_pad + 1, g_in_game ? "pause menu" : "main menu");
    g_host->open_page("controller", g_in_game, &back_to_controls);
    return true;
}

}  // namespace pad::controls_options
