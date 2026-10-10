// The game's network calls (Winsock): a log of them, and what two copies on one PC need to find each other.
#pragma once

namespace dev_tools::network {

// Hooks the Winsock functions the game uses. With `write_log`, the first few hundred calls are logged. With `reuse_address`,
// datagram sockets get SO_REUSEADDR before they are bound, so that two copies on one PC can use the same port. With
// `alias_other_copy`, the other copy's LAN announcements reach the game as coming from another address (see network.cpp).
void install(bool write_log, bool reuse_address, bool alias_other_copy);

}  // namespace dev_tools::network
