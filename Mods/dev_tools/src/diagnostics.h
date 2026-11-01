// Finding out what ends the game: where it calls ExitProcess / TerminateProcess, and where it has an access violation.
#pragma once

namespace dev_tools::diagnostics {

// The exception logger and the ExitProcess / TerminateProcess hooks: from the early entry, so that a crash during the boot flow is logged too.
void install_early();

// The rest (the TLS watcher), from the main entry; also does install_early if it has not run.
void install();

// Logs the values on the stack from `sp` up that look like return addresses into the game or the mods ("    stack:" lines).
void log_stack(const void *sp);

}  // namespace dev_tools::diagnostics
