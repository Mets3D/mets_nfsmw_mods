// A log of the game's network calls (Winsock): sockets, binds, sends, the packets received. Nothing is changed.
#pragma once

namespace dev_tools::network {

// Hooks the Winsock functions the game uses and logs the first few hundred calls.
void install();

}  // namespace dev_tools::network
