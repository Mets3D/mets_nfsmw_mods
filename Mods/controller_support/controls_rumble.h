// The Controls screen's "Rumble" hint (the game's rumble on / off toggle, shown while a controller is selected) opens Options >
// Rumble for the selected controller instead, and leaving that page comes back to the Controls screen with the same controller
// selected. The Controls screen's message handler and device switch are pad_sets.cpp's hooks; they call into here.
#pragma once

#include "custom_menus_api.h"
#include "pad.h"

namespace pad::controls_rumble {

void provide(const nfsmw::custom_menus::Host *host);  // the hint's new text, and what we need to open the page later

// The Controls screen's message handler, before the game's: returns the message the game should get. The hint's becomes the
// shoulder button's, whose path asks about unsaved binding changes and ends in the device switch (on_device_switch).
unsigned filter_message(void *screen, unsigned message);
// The device switch (the shoulder buttons' action, which the hint starts after the game's question about unsaved changes): true
// when it was the hint's, and the page is opening instead.
bool on_device_switch(void *screen);

}  // namespace pad::controls_rumble
