// When the improved AI of a player's car burns nitrous (see nitro.cpp).
#pragma once

namespace improved_ai_opponents::nitro {

// Called where AIActionRace::Update hands its decision to the car's input: `action` is the AIActionRace, `frame` its Update's stack frame (its locals), `game_wants` what the
// game decided. Returns whether the nitrous is to be on.
bool decide(int player, const unsigned char *action, const unsigned char *frame, bool game_wants);

}  // namespace improved_ai_opponents::nitro
