// The rear view mirror per camera view (the page's "Mirror"). The game shows it in the bumper and hood views only, when the
// Rearview option is on; this can hide it in those and show it in the others.
#pragma once

#include "nfsmw_util.h"

namespace camera_settings::mirror {

// FEngHud::ShouldRearViewMirrorBeVisible (the mirror's frame, and with MaybeChangeViewMode the view it draws) and
// CubicCameraMover::OutsidePOV as MaybeChangeViewMode asks it.
void install_hooks(nfsmw::Logger &log, const char *ini);

}  // namespace camera_settings::mirror
