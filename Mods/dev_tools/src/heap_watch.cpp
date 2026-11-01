/*
 * A watch on the entries of FEPackageManager's list of FEng packages (FEPackageData, 0x38 bytes), for hunting a corrupted entry: the walks of that list
 * (BroadcastMessage, NotificationMessage, FindFEPackageData) crash on an entry that was freed or written over (agent_docs/project.md, "Reading a heap corruption").
 * The game's heap is the debug CRT's: each block has a header before it (data size -0x10, use -0xC, request number -8, no-man's land -4), and every free goes through
 * __free_dbg_lk 0x7C7300. Three checks:
 *   - a free of an entry during a walk of the list (the walk goes on from a freed entry) is logged with the message and the stack of the free;
 *   - every frame, before FEPackageManager::Tick walks the list, each entry's header and links are compared with what they were when first seen;
 *   - the pages of the entries are read-only, and a write into an entry by code other than the package manager's own is logged with its writer and stack; every
 *     write is let through (the page is writable for one single-stepped instruction, then read-only again).
 */
#include "heap_watch.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "dev_tools.h"
#include "diagnostics.h"
#include "hooks.h"

namespace dev_tools::heap_watch {

namespace {

constexpr uintptr_t kManager = 0x91D21C;  // FEPackageManager::mInstance: the list's head at +4 (next, prev); a node is at +4 of its entry
constexpr unsigned kEntrySize = 0x38, kNoMansLand = 0xFDFDFDFD;
constexpr int kNormalBlock = 1, kMaxEntries = 1024, kMaxCaught = 8, kMaxWrites = 12;
constexpr uintptr_t kPage = 0x1000;
constexpr DWORD kTrapFlag = 0x100;

struct Node {
    Node *next, *prev;
};
struct Header {
    unsigned size;
    int use;
    long request;
    unsigned gap;
};
const Header &header(const void *block) { return *reinterpret_cast<const Header *>(static_cast<const char *>(block) - sizeof(Header)); }

Node *head() {
    char *manager = *reinterpret_cast<char **>(kManager);
    return manager ? reinterpret_cast<Node *>(manager + 4) : nullptr;
}
const void *entry_of(const Node *node) { return reinterpret_cast<const char *>(node) - 4; }

CRITICAL_SECTION g_lock;
std::unordered_map<const void *, long> g_seen;  // each entry and the request number of its block when it was first seen
int g_caught = 0;
bool g_reported = false;
unsigned g_frame = 0;
unsigned g_walk_message = 0;  // the message of the walk of the list in progress, on g_walk_thread
int g_walks = 0;
DWORD g_walk_thread = 0;

// The package: the short name in an uncompressed chunk's data, the hash of the name in a compressed one.
void describe(const void *entry, char *out, size_t size) {
    const auto *chunk = *reinterpret_cast<const unsigned char *const *>(static_cast<const char *>(entry) + 0x18);
    if (!chunk || IsBadReadPtr(chunk, 0x50)) snprintf(out, size, "chunk %p", static_cast<const void *>(chunk));
    else if (*reinterpret_cast<const unsigned *>(chunk) == 0x30203) snprintf(out, size, "\"%.32s\"", reinterpret_cast<const char *>(chunk + 8 + 0x28));
    else snprintf(out, size, "name hash %08x", *reinterpret_cast<const unsigned *>(chunk + 8));
}

bool in_list(const void *block) {
    Node *h = head();
    int n = 0;
    for (Node *node = h ? h->next : nullptr; node && node != h && n < kMaxEntries; node = node->next, ++n)
        if (entry_of(node) == block) return true;
    return false;
}

void (__cdecl *g_orig_free)(void *, int);

void __cdecl hook_free(void *block, int use) {
    if (block && header(block).size == kEntrySize) {
        if (g_caught < kMaxCaught && in_list(block)) {
            ++g_caught;
            char name[64];
            describe(block, name, sizeof(name));
            g_log.line("heap watch: frame %u, thread %lu: the package entry %p (%s, allocation %ld) is freed while it is still in the package manager's list; the stack of the free:",
                       g_frame, GetCurrentThreadId(), block, name, header(block).request);
            diagnostics::log_stack(__builtin_frame_address(0));
        }
        EnterCriticalSection(&g_lock);
        const bool known = g_seen.erase(block) != 0;
        LeaveCriticalSection(&g_lock);
        if (known && g_walks > 0 && GetCurrentThreadId() == g_walk_thread) {
            char name[64];
            describe(block, name, sizeof(name));
            g_log.line("heap watch: frame %u: the entry %p (%s, allocation %ld) is freed DURING a walk of the list with the message %08x: the walk goes on from a freed entry; "
                       "the stack of the free:", g_frame, block, name, header(block).request, g_walk_message);
            diagnostics::log_stack(__builtin_frame_address(0));
        }
    }
    g_orig_free(block, use);
}

void dump_list() {
    Node *h = head();
    int n = 0;
    for (Node *node = h ? h->next : nullptr; node && node != h && n < kMaxEntries && !IsBadReadPtr(node, sizeof(Node)); node = node->next, ++n) {
        const void *entry = entry_of(node);
        char name[64];
        describe(entry, name, sizeof(name));
        g_log.line("heap watch:   entry %p: %s, allocation %ld (%u bytes, use %d), screen %p, next %p", entry, name, header(entry).request, header(entry).size, header(entry).use,
                   *reinterpret_cast<void *const *>(static_cast<const char *>(entry) + 0x20), static_cast<void *>(node->next));
    }
}

// Each entry's block is still its own (the request number it had, a normal block, its no-man's land) and links back, before the game walks the list.
void check_list() {
    Node *h = head();
    if (!h || g_reported) return;
    int n = 0;
    const char *problem = nullptr;
    const void *bad = nullptr;
    EnterCriticalSection(&g_lock);
    for (Node *node = h->next; node != h; node = node->next) {
        if (!node || ++n > kMaxEntries || IsBadReadPtr(node, sizeof(Node))) {
            problem = "a link of the list points nowhere";
            break;
        }
        const void *entry = entry_of(node);
        const Header &hd = header(entry);
        const long known = g_seen.try_emplace(entry, hd.request).first->second;
        bad = entry;
        if (hd.request != known) problem = "its block was freed and allocated again (another request number)";
        else if (hd.use != kNormalBlock || hd.size != kEntrySize) problem = "its block is not an entry's any more (use or size)";
        else if (hd.gap != kNoMansLand) problem = "something wrote over the start of its block (no-man's land)";
        else if (!node->next || IsBadReadPtr(node->next, sizeof(Node)) || node->next->prev != node) problem = "its link to the next entry is broken";
        if (problem) break;
    }
    LeaveCriticalSection(&g_lock);
    if (!problem) return;
    g_reported = true;
    char name[64] = "?";
    if (bad) describe(bad, name, sizeof(name));
    g_log.line("heap watch: frame %u: the entry %p (%s) of the package manager's list: %s (allocation %ld, %u bytes, use %d); the list:", g_frame, bad, name, problem,
               bad ? header(bad).request : 0, bad ? header(bad).size : 0, bad ? header(bad).use : 0);
    dump_list();
}

// The walks of the list that call the screens' handlers on the way (their message is logged with a free during them).
struct Walk {
    unsigned saved_message;
    explicit Walk(unsigned message) : saved_message(g_walk_message) {
        g_walk_thread = GetCurrentThreadId();
        g_walk_message = message;
        ++g_walks;
    }
    ~Walk() {
        --g_walks;
        g_walk_message = saved_message;
    }
};

void(__fastcall *g_orig_notification)(void *, void *, unsigned, void *, unsigned, unsigned);
void __fastcall hook_notification(void *self, void *edx, unsigned message, void *object, unsigned a, unsigned b) {
    Walk walk(message);
    g_orig_notification(self, edx, message, object, a, b);
}

void(__fastcall *g_orig_broadcast)(void *, void *, unsigned);
void __fastcall hook_broadcast(void *self, void *edx, unsigned message) {
    Walk walk(message);
    g_orig_broadcast(self, edx, message);
}

// ---- The writer. Per thread (TLS slots, no allocation in the exception handler): the pages made writable for the instruction being stepped
// and the entry field it was logged writing (its new value is logged after the step).
std::unordered_map<uintptr_t, bool> g_pages;  // the pages made read-only (guarded by g_lock)
DWORD g_tls_page[2], g_tls_count, g_tls_logged;
int g_writes_logged = 0;
bool g_armed = false;
PVOID g_handler = nullptr;

uintptr_t tls(DWORD slot) { return reinterpret_cast<uintptr_t>(TlsGetValue(slot)); }
void set_tls(DWORD slot, uintptr_t value) { TlsSetValue(slot, reinterpret_cast<void *>(value)); }

void protect(uintptr_t page, DWORD mode) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void *>(page), kPage, mode, &old);
}

// The package manager's and its entries' own code (FEPackageData, FEPackageManager), which writes the entries.
bool own_code(uintptr_t eip) {
    return (eip >= 0x585400 && eip < 0x585B50) || (eip >= 0x591D50 && eip < 0x591E70) || (eip >= 0x5A8B00 && eip < 0x5A8C80) || (eip >= 0x572070 && eip < 0x5720E0);
}

const void *entry_at(uintptr_t address) {
    for (const auto &seen : g_seen) {
        const auto entry = reinterpret_cast<uintptr_t>(seen.first);
        if (address >= entry && address < entry + kEntrySize) return seen.first;
    }
    return nullptr;
}

void module_of(uintptr_t address, char *out, size_t size) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module) && module)
        GetModuleFileNameA(module, path, sizeof(path));
    const char *slash = strrchr(path, '\\');
    snprintf(out, size, "%s+%#lx", slash ? slash + 1 : path, static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(module)));
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;
    const int open = static_cast<int>(tls(g_tls_count));
    // After the one instruction (or one iteration of a rep string instruction, whose next one faults again): read-only again, if the page is still watched.
    if (record->ExceptionCode == EXCEPTION_SINGLE_STEP && open) {
        if (const uintptr_t logged = tls(g_tls_logged)) {
            g_log.line("heap watch:   ... it wrote %08x", *reinterpret_cast<const unsigned *>(logged & ~3u));
            set_tls(g_tls_logged, 0);
        }
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < open; ++i)
            if (g_pages.count(tls(g_tls_page[i]))) protect(tls(g_tls_page[i]), PAGE_READONLY);
        LeaveCriticalSection(&g_lock);
        set_tls(g_tls_count, 0);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 || record->ExceptionInformation[0] != 1 || open >= 2) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t address = record->ExceptionInformation[1], page = address & ~(kPage - 1);
    EnterCriticalSection(&g_lock);
    const bool watched = g_pages.count(page) != 0;
    const void *entry = watched ? entry_at(address) : nullptr;
    LeaveCriticalSection(&g_lock);
    if (!watched) return EXCEPTION_CONTINUE_SEARCH;
    if (entry && !own_code(context->Eip) && g_writes_logged < kMaxWrites) {
        ++g_writes_logged;
        char name[64], writer[96];
        describe(entry, name, sizeof(name));
        module_of(context->Eip, writer, sizeof(writer));
        g_log.line("heap watch: frame %u, thread %lu: %s (eip %#lx) writes +%#x of the entry %p (%s; +4 is its next, +8 its previous), which holds %08x; the stack:", g_frame,
                   GetCurrentThreadId(), writer, static_cast<unsigned long>(context->Eip), static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(entry)), entry, name,
                   *reinterpret_cast<const unsigned *>(address & ~3u));
        diagnostics::log_stack(reinterpret_cast<const void *>(context->Esp));
        set_tls(g_tls_logged, address);
    }
    protect(page, PAGE_READWRITE);
    set_tls(g_tls_page[open], page);
    set_tls(g_tls_count, open + 1);
    context->EFlags |= kTrapFlag;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// The pages of the entries read-only, every frame: new entries' pages are added, and the pages that hold none any more (the race's load unloads the front end's
// packages) are given back.
void arm() {
    if (!g_handler) return;
    EnterCriticalSection(&g_lock);
    int added = 0;
    for (auto &page : g_pages) page.second = false;
    for (const auto &seen : g_seen) {
        const auto entry = reinterpret_cast<uintptr_t>(seen.first);
        for (uintptr_t page = entry & ~(kPage - 1); page < entry + kEntrySize; page += kPage) {
            const auto made = g_pages.emplace(page, true);
            made.first->second = true;
            if (made.second) {
                protect(page, PAGE_READONLY);
                ++added;
            }
        }
    }
    for (auto page = g_pages.begin(); page != g_pages.end();) {
        if (page->second) {
            ++page;
            continue;
        }
        protect(page->first, PAGE_READWRITE);
        page = g_pages.erase(page);
    }
    LeaveCriticalSection(&g_lock);
    if (added && !g_armed) g_log.line("heap watch: frame %u: the %d pages of the package entries are read-only from now on", g_frame, added);
    g_armed = true;
}

void(__fastcall *g_orig_tick)(void *, void *);

void __fastcall hook_tick(void *self, void *edx) {
    ++g_frame;
    check_list();
    if (!g_reported) arm();
    g_orig_tick(self, edx);
}

}  // namespace

void install() {
    InitializeCriticalSection(&g_lock);
    g_tls_page[0] = TlsAlloc();
    g_tls_page[1] = TlsAlloc();
    g_tls_count = TlsAlloc();
    g_tls_logged = TlsAlloc();
    if (g_tls_page[0] != TLS_OUT_OF_INDEXES && g_tls_page[1] != TLS_OUT_OF_INDEXES && g_tls_count != TLS_OUT_OF_INDEXES && g_tls_logged != TLS_OUT_OF_INDEXES)
        g_handler = AddVectoredExceptionHandler(1, &on_exception);  // (first: the crash logger must not see these)
    if (!g_handler) g_log.line("heap watch: no exception handler: the writes into the entries are not watched");
    const nfsmw::Hook hooks[] = {
        {"heapwatch_free", "__free_dbg_lk", 0x7C7300, {0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x14}, false, nfsmw::detour(&hook_free), nfsmw::orig(&g_orig_free)},
        {"heapwatch_tick", "FEPackageManager::Tick", 0x5A8C20, {0x56, 0x8B, 0xF1, 0xB9, 0x50, 0x7F, 0x98}, false, nfsmw::detour(&hook_tick), nfsmw::orig(&g_orig_tick)},
        {"heapwatch_notify", "FEPackageManager::NotificationMessage", 0x5A8BA0, {0x8B, 0x41, 0x04, 0x85, 0xC0, 0x53, 0x55}, false, nfsmw::detour(&hook_notification),
         nfsmw::orig(&g_orig_notification)},
        {"heapwatch_broadcast", "FEPackageManager::BroadcastMessage", 0x5A8B00, {0x8B, 0x41, 0x04, 0x81, 0xEC, 0x80, 0x00}, false, nfsmw::detour(&hook_broadcast),
         nfsmw::orig(&g_orig_broadcast)},
    };
    nfsmw::install_hooks(hooks, 4, g_log, g_ini, "heap watch");
}

}  // namespace dev_tools::heap_watch
