// NFSMW_PLUGIN_EARLY() { ... return 0; }: a plugin's body that runs at once when the .asi is loaded (see entry.c), before
// the game's own code has run and before NFSMW_PLUGIN_MAIN. Use it only for what cannot wait; the rest belongs in
// NFSMW_PLUGIN_MAIN. At most once per plugin.
#pragma once

#define NFSMW_PLUGIN_EARLY()                                                                    \
    static int nfsmw_plugin_early_impl(void);                                                   \
    extern "C" { int (*nfsmw_plugin_early_ptr)(void) = nfsmw_plugin_early_impl; }               \
    static int nfsmw_plugin_early_impl(void)
