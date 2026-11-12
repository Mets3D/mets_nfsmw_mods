// DualSense adaptive triggers, through DSX (DualSenseX): in a race the brake and accelerator triggers resist like pedals, and the
// accelerator's kicks on a gear shift. See dualsense.cpp.
#pragma once

namespace pad::dualsense {

// From the game thread: which trigger (0 left, 1 right, -1 none) the accelerate and brake commands are on, for the player who holds
// the DualSense.
void set_triggers(int accelerator, int brake);
// The gear shift's kick for the accelerator trigger (only used while takes_shift()): the Gear Shifts level, 0..1, and the kick's
// strength now, 0..1 (0 when there is none).
void set_shift(float level, float shape);
// DSX is running and the accelerator is on a trigger: the gear shift's rumble belongs to that trigger, not to the controller's motors.
bool takes_shift();

// Called often by the rumble thread: whether the DualSense's player is in a running race, and whether the Settings page's sample of the
// gear shift kick is playing on the trigger (set_shift has its level and shape, no resistance then). Sends to DSX when that or the settings change.
void update(bool racing, bool previewing);

}  // namespace pad::dualsense
