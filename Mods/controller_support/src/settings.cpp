// mets_controller_support.ini, the log, and the settings every part reads.
#include "button_icons.h"
#include "default_layout.h"
#include "look_options.h"
#include "device_settings.h"
#include "menu_buttons.h"
#include "pad.h"
#include "rumble.h"

namespace pad {

nfsmw::Logger g_log;

void log_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    g_log.vline(fmt, ap);
    va_end(ap);
}

char g_ini[MAX_PATH];
Settings g_cfg;
LookSettings g_look_cfg;

float ini_float(const char *key, float def, const char *section) {
    char buf[32] = {};
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_ini);
    return buf[0] ? static_cast<float>(atof(buf)) : def;
}

void load_settings() {
    nfsmw::data_file("mets_controller_support.ini", g_ini, sizeof(g_ini));
    g_cfg.enabled = GetPrivateProfileIntA("pad", "enabled", 1, g_ini) != 0;
    LookSettings &l = g_look_cfg;
    l.enabled = GetPrivateProfileIntA("look", "enabled", 1, g_ini) != 0;
    l.stick_speed = ini_float("stick_speed", l.stick_speed, "look");
    l.mouse_sensitivity = ini_float("mouse_sensitivity", l.mouse_sensitivity, "look");
    l.invert_y = GetPrivateProfileIntA("look", "invert_y", 0, g_ini) != 0;
    l.return_delay = ini_float("return_delay", l.return_delay, "look");
    l.return_speed = ini_float("return_speed", l.return_speed, "look");
    l.min_pitch = ini_float("min_pitch", l.min_pitch, "look");
    l.max_pitch = ini_float("max_pitch", l.max_pitch, "look");
    log_line("look: enabled=%d stick_speed=%.0f mouse_sensitivity=%.2f invert_y=%d return after %.1fs at %.1f, "
             "pitch %.0f..%.0f", l.enabled, l.stick_speed, l.mouse_sensitivity, l.invert_y, l.return_delay,
             l.return_speed, l.min_pitch, l.max_pitch);
    look_options::load();
    button_icons::load_settings();
    default_layout::load_settings();
    rumble::load_settings();
    device_settings::load();
    log_line("settings: enabled=%d", g_cfg.enabled);
}

}  // namespace pad
