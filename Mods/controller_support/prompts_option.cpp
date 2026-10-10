#include "prompts_option.h"

#include "button_icons.h"
#include "pad.h"

namespace pad::prompts_option {

namespace opt = nfsmw::custom_menus;
using button_icons::Style;

namespace {

Style g_opened = Style::kXbox;  // the style when the screen opened: what was last accepted (and is in the ini)

const opt::Row kRows[] = {
    {opt::kValueRow, "Controller Prompts", 0, "Which buttons the menus show for a controller: Xbox or PlayStation."},
};

unsigned value(int, char *out, unsigned out_size) {
    const Style style = button_icons::style();
    snprintf(out, out_size, "%s", style == Style::kXbox ? "Xbox" : style == Style::kPlayStation ? "PlayStation" : "Off");
    return 0;
}

void change(int, int, bool) {  // two choices: either arrow takes the other one (from Off, to Xbox)
    button_icons::set_style(button_icons::style() == Style::kXbox ? Style::kPlayStation : Style::kXbox);
}

void opened() { g_opened = button_icons::style(); }
bool changed() { return button_icons::style() != g_opened; }
void accept() {
    button_icons::save_style();
    g_opened = button_icons::style();
}
void discard() { button_icons::set_style(g_opened); }
void defaults() { button_icons::set_style(Style::kXbox); }

opt::Page g_page;

}  // namespace

void provide(const opt::Host *host) {
    g_page = {"prompts", "", "", "", 0, 0, nullptr, 1, kRows, nullptr, opened, value, change, changed, accept, discard,
              defaults, opt::kPlayerOptions};
    host->register_page(&g_page);
}

}  // namespace pad::prompts_option
