// Hears from Windows when a device is plugged in or out, so that controllers are looked for only then (common/device_scan.h).
#pragma once

namespace pad::device_watch {

// Starts the thread that listens (a message-only window registered for device notifications). Until it is listening, and if it cannot,
// the looks for controllers keep their pace of every 2 seconds.
void start();

}  // namespace pad::device_watch
