// One set of controller bindings per game controller, and the Controls menu's list of them.
//
// The game's binding table has one set of controller slots (2 and 3 of each entry) and every rebind writes controller
// number 0, whichever controller was pressed; its Controls screen can only switch between the keyboard and controller 0.
// Here every controller the game has (its joystick table, 0x91F150) gets a set of its own: controller 0's is the game's
// (the live table, which the profile saves), the others are kept here and in the ini ([pad_bindings]). A controller whose
// set was never edited uses controller 0's. The Controls screen cycles through the keyboard and all controllers; while one
// of them is shown its set is swapped into the live table, and swapped back when the screen closes.
#pragma once

#include "pad.h"

namespace pad::pad_sets {

int joystick_count();                  // controllers in the game's joystick table
void *joystick_di_device(int index);   // the DirectInput device of one (the game's own object has it at +0x468)
int joystick_of_device(void *di_device);  // its index in the table, or -1
bool actions_of_slot(int xinput_slot, float *values);  // the entries' values a controller asks for through its set
bool actions_of_keyboard(float *values);  // the same for the keyboard (the keyboard slots of the live table)
int live_pad();                        // whose set the live table holds (0 unless the Controls screen shows another)
void add_values(float *entry_values);  // after the game polled: what the other controllers' sets say, per entry
void load_settings();
void install_hooks();

}  // namespace pad::pad_sets
