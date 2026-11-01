// A clock for each racer of a split screen tollbooth race: the booths' time goes to the racer who crossed, and a racer whose time is up is knocked out (see tollbooth.cpp).
#pragma once

namespace split_screen_support::tollbooth {

void install();

// A split screen tollbooth race is running (and the clocks are on).
bool active();
// What the racer has left; from the finish on, what they had left at the finish.
float time_left(int racer);

// The Time row of the split screen race options: the time a split screen tollbooth race starts with and its booths' bonuses, in percent of the game's.
void set_time_percent(int percent);

// While it lives, the game's questions about the time left are about this racer.
class Asking {
  public:
    explicit Asking(int racer);
    ~Asking();

  private:
    int previous_;
};

}  // namespace split_screen_support::tollbooth
