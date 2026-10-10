#include "network.h"

#include "dev_tools.h"

#include <MinHook.h>

namespace dev_tools::network {

namespace {

// Winsock's types, declared here so that winsock2.h (which clashes with windows.h's older winsock.h) is not needed.
using SOCKET = UINT_PTR;
struct SockAddrIn {  // sockaddr_in
    unsigned short family;
    unsigned short port;  // network order
    unsigned char addr[4];
    char zero[8];
};
struct WsaBuf {
    ULONG len;
    char *buf;
};

nfsmw::Logger *g_logger = nullptr;
volatile LONG g_budget = 800;  // lines left: the game sends all the time

bool allowed() { return InterlockedDecrement(&g_budget) >= 0; }

const char *describe(const void *address, int length, char *out, size_t out_size) {
    if (address && length >= static_cast<int>(sizeof(SockAddrIn))) {
        const SockAddrIn *a = static_cast<const SockAddrIn *>(address);
        snprintf(out, out_size, "%u.%u.%u.%u:%u (family %u)", a->addr[0], a->addr[1], a->addr[2], a->addr[3],
                 ((a->port & 0xFF) << 8) | (a->port >> 8), a->family);
    } else {
        snprintf(out, out_size, "(none)");
    }
    return out;
}

const char *hex_prefix(const char *data, int length, char *out, size_t out_size) {
    size_t n = 0;
    for (int i = 0; data && i < length && i < 12 && n + 4 < out_size; ++i)
        n += snprintf(out + n, out_size - n, "%02x ", static_cast<unsigned char>(data[i]));
    out[n] = 0;
    return out;
}

const char *option_name(int level, int name) {
    if (level == 0xFFFF) {  // SOL_SOCKET
        switch (name) {
            case 0x0004: return "SO_REUSEADDR";
            case 0x0020: return "SO_BROADCAST";
            case 0x1001: return "SO_SNDBUF";
            case 0x1002: return "SO_RCVBUF";
            case 0x0080: return "SO_LINGER";
            case 0x0100: return "SO_DONTLINGER";
        }
    }
    return "?";
}

unsigned long id(SOCKET s) { return static_cast<unsigned long>(s); }

// ------------------------------------------------------------------------------------------------
// Two copies on one PC have the same address. The game's LAN announcements (UDP port 9999, "gEA" packets) reach both, but a
// copy that sees a packet from its own address takes it for its own echo and drops it. So a packet that this copy did not
// send gets a made-up source address (127.0.0.2) on its way in, and the game's connections to that address are sent to the
// PC's real one on their way out.
// ------------------------------------------------------------------------------------------------
constexpr unsigned char kPeerAddress[4] = {127, 0, 0, 2};
bool g_alias_other_copy = true;
unsigned char g_real_address[4] = {};
bool g_have_real_address = false;
unsigned char g_sent[16][12];  // the first 12 bytes of the announcements this copy sent (they hold a random id)
int g_sent_count = 0;
CRITICAL_SECTION g_alias_lock;
volatile LONG g_alias_logged = 0;

bool is_announcement(const char *data, int length) { return data && length >= 12 && data[0] == 'g' && data[1] == 'E' && data[2] == 'A'; }

void note_sent(const char *data, int length) {
    if (!is_announcement(data, length)) return;
    EnterCriticalSection(&g_alias_lock);
    bool known = false;
    for (int i = 0; i < g_sent_count; ++i) known |= memcmp(g_sent[i], data, 12) == 0;
    if (!known && g_sent_count < 16) memcpy(g_sent[g_sent_count++], data, 12);
    LeaveCriticalSection(&g_alias_lock);
}

// A received announcement: if this copy did not send it, the other copy did: its source address becomes the made-up one.
void alias_if_foreign(const char *data, int length, void *from, int from_length) {
    if (!g_alias_other_copy || !is_announcement(data, length) || !from || from_length < static_cast<int>(sizeof(SockAddrIn))) return;
    SockAddrIn *address = static_cast<SockAddrIn *>(from);
    bool ours = false;
    EnterCriticalSection(&g_alias_lock);
    for (int i = 0; i < g_sent_count && !ours; ++i) ours = memcmp(g_sent[i], data, 12) == 0;
    if (ours && !g_have_real_address && memcmp(address->addr, kPeerAddress, 4) != 0) {
        memcpy(g_real_address, address->addr, 4);
        g_have_real_address = true;
    }
    LeaveCriticalSection(&g_alias_lock);
    if (ours) return;
    if (InterlockedIncrement(&g_alias_logged) <= 3 && g_logger)
        g_logger->line("net: an announcement from the other copy (%u.%u.%u.%u) is shown to the game as coming from 127.0.0.2",
                    address->addr[0], address->addr[1], address->addr[2], address->addr[3]);
    memcpy(address->addr, kPeerAddress, 4);
}

// An address the game uses for sending: the made-up one stands for this PC.
const void *unalias(const void *to, int length, SockAddrIn *copy) {
    if (!g_alias_other_copy || !g_have_real_address || !to || length < static_cast<int>(sizeof(SockAddrIn))) return to;
    const SockAddrIn *address = static_cast<const SockAddrIn *>(to);
    if (address->family != 2 || memcmp(address->addr, kPeerAddress, 4) != 0) return to;
    *copy = *address;
    memcpy(copy->addr, g_real_address, 4);
    return copy;
}

using SocketFn = SOCKET(WINAPI *)(int, int, int);
SocketFn g_orig_socket;
SOCKET WINAPI hook_socket(int af, int type, int protocol) {
    const SOCKET s = g_orig_socket(af, type, protocol);
    const DWORD err = GetLastError();
    if (allowed()) g_logger->line("net: socket(af %d, type %d, protocol %d) = %lu%s", af, type, protocol, id(s), s == static_cast<SOCKET>(-1) ? " FAILED" : "");
    SetLastError(err);
    return s;
}

using BindFn = int(WINAPI *)(SOCKET, const void *, int);
using SetSockOptFn = int(WINAPI *)(SOCKET, int, int, const char *, int);
using GetSockOptFn = int(WINAPI *)(SOCKET, int, int, char *, int *);
BindFn g_orig_bind;
SetSockOptFn g_orig_setsockopt;
GetSockOptFn g_get_sockopt;
bool g_reuse_address = true;

// The game binds its LAN socket (UDP, 0.0.0.0:9999) without SO_REUSEADDR, so a second copy on the same PC cannot bind it
// and never hears the first one. With the option set before the bind, both copies get the broadcasts.
void allow_shared_bind(SOCKET s) {
    constexpr int kSolSocket = 0xFFFF, kSoReuseAddr = 4, kSoType = 0x1008, kSockDgram = 2;
    int type = 0, size = sizeof(type);
    if (!g_get_sockopt || g_get_sockopt(s, kSolSocket, kSoType, reinterpret_cast<char *>(&type), &size) != 0 || type != kSockDgram) return;
    const int on = 1;
    const int r = g_orig_setsockopt(s, kSolSocket, kSoReuseAddr, reinterpret_cast<const char *>(&on), sizeof(on));
    if (allowed()) g_logger->line("net: SO_REUSEADDR added to datagram socket %lu before its bind = %d", static_cast<unsigned long>(s), r);
}

int WINAPI hook_bind(SOCKET s, const void *name, int length) {
    if (g_reuse_address) allow_shared_bind(s);
    const int r = g_orig_bind(s, name, length);
    const DWORD err = GetLastError();
    char text[80];
    if (allowed()) g_logger->line("net: bind(%lu, %s) = %d", id(s), describe(name, length, text, sizeof(text)), r);
    if (r && allowed()) g_logger->line("net:   bind failed, error %lu (10048 = the address is already in use)", err);
    SetLastError(err);
    return r;
}

int WINAPI hook_setsockopt(SOCKET s, int level, int name, const char *value, int length) {
    const int r = g_orig_setsockopt(s, level, name, value, length);
    const DWORD err = GetLastError();
    if (allowed())
        g_logger->line("net: setsockopt(%lu, level %#x, %s (%#x), value %d) = %d", id(s), level, option_name(level, name), name,
                    value && length >= 4 ? *reinterpret_cast<const int *>(value) : -1, r);
    SetLastError(err);
    return r;
}

using SendToFn = int(WINAPI *)(SOCKET, const char *, int, int, const void *, int);
SendToFn g_orig_sendto;
int WINAPI hook_sendto(SOCKET s, const char *data, int length, int flags, const void *to, int to_length) {
    SockAddrIn real;
    const int r = g_orig_sendto(s, data, length, flags, unalias(to, to_length, &real), to_length);
    const DWORD err = GetLastError();
    if (r > 0) note_sent(data, length);
    char text[80], hex[48];
    if (allowed())
        g_logger->line("net: sendto(%lu, %d bytes [%s], %s) = %d%s", id(s), length, hex_prefix(data, length, hex, sizeof(hex)),
                    describe(to, to_length, text, sizeof(text)), r, r < 0 ? " FAILED" : "");
    if (r < 0 && allowed()) g_logger->line("net:   sendto failed, error %lu", err);
    SetLastError(err);
    return r;
}

using ConnectFn = int(WINAPI *)(SOCKET, const void *, int);
ConnectFn g_orig_connect;
int WINAPI hook_connect(SOCKET s, const void *name, int length) {
    SockAddrIn real;
    const int r = g_orig_connect(s, unalias(name, length, &real), length);
    const DWORD err = GetLastError();
    char text[80];
    if (allowed()) g_logger->line("net: connect(%lu, %s) = %d (error %lu)", id(s), describe(name, length, text, sizeof(text)), r, r ? err : 0);
    SetLastError(err);
    return r;
}

using ListenFn = int(WINAPI *)(SOCKET, int);
ListenFn g_orig_listen;
int WINAPI hook_listen(SOCKET s, int backlog) {
    const int r = g_orig_listen(s, backlog);
    const DWORD err = GetLastError();
    if (allowed()) g_logger->line("net: listen(%lu, %d) = %d", id(s), backlog, r);
    SetLastError(err);
    return r;
}

// Receiving is overlapped (WSARecvFrom with events): the buffer and address of each posted receive are remembered, and
// logged when WSAGetOverlappedResult says the receive has completed.
struct PostedReceive {
    const void *overlapped;
    const char *data;
    const void *from;
    const int *from_length;
};
PostedReceive g_posted[32];
CRITICAL_SECTION g_posted_lock;

void remember(const void *overlapped, const char *data, const void *from, const int *from_length) {
    EnterCriticalSection(&g_posted_lock);
    int free_slot = -1;
    for (int i = 0; i < 32; ++i) {
        if (g_posted[i].overlapped == overlapped) free_slot = i;
        else if (free_slot < 0 && !g_posted[i].overlapped) free_slot = i;
    }
    if (free_slot >= 0) g_posted[free_slot] = {overlapped, data, from, from_length};
    LeaveCriticalSection(&g_posted_lock);
}

bool take(const void *overlapped, PostedReceive *out) {
    bool found = false;
    EnterCriticalSection(&g_posted_lock);
    for (int i = 0; i < 32 && !found; ++i)
        if (g_posted[i].overlapped == overlapped) {
            *out = g_posted[i];
            g_posted[i].overlapped = nullptr;
            found = true;
        }
    LeaveCriticalSection(&g_posted_lock);
    return found;
}

using WsaRecvFromFn = int(WINAPI *)(SOCKET, WsaBuf *, DWORD, DWORD *, DWORD *, void *, int *, void *, void *);
WsaRecvFromFn g_orig_WSARecvFrom;
int WINAPI hook_WSARecvFrom(SOCKET s, WsaBuf *buffers, DWORD count, DWORD *received, DWORD *flags, void *from,
                            int *from_length, void *overlapped, void *completion) {
    const int r = g_orig_WSARecvFrom(s, buffers, count, received, flags, from, from_length, overlapped, completion);
    const DWORD err = GetLastError();
    static LONG posted = 0;
    char text[80];
    if (r == 0) {
        if (received && buffers) alias_if_foreign(buffers[0].buf, static_cast<int>(*received), from, from_length ? *from_length : 0);
        if (allowed())
            g_logger->line("net: WSARecvFrom(%lu) = 0, %lu bytes from %s", id(s), received ? static_cast<unsigned long>(*received) : 0,
                        describe(from, from_length ? *from_length : 0, text, sizeof(text)));
    } else if (overlapped && err == 997) {
        remember(overlapped, buffers ? buffers[0].buf : nullptr, from, from_length);
        if (InterlockedIncrement(&posted) <= 3 && allowed())
            g_logger->line("net: WSARecvFrom(%lu) posted (error 997 = waiting for a packet)", id(s));
    }
    SetLastError(err);
    return r;
}

using GetOverlappedResultFn = BOOL(WINAPI *)(SOCKET, void *, DWORD *, BOOL, DWORD *);
GetOverlappedResultFn g_orig_WSAGetOverlappedResult;
BOOL WINAPI hook_WSAGetOverlappedResult(SOCKET s, void *overlapped, DWORD *transferred, BOOL wait, DWORD *flags) {
    const BOOL r = g_orig_WSAGetOverlappedResult(s, overlapped, transferred, wait, flags);
    const DWORD err = GetLastError();
    PostedReceive receive;
    if (r && transferred && *transferred && take(overlapped, &receive)) {
        alias_if_foreign(receive.data, static_cast<int>(*transferred), const_cast<void *>(receive.from),
                         receive.from_length ? *receive.from_length : 0);
        char text[80], hex[48];
        if (allowed())
            g_logger->line("net: received %lu bytes [%s] on %lu from %s", static_cast<unsigned long>(*transferred),
                        hex_prefix(receive.data, static_cast<int>(*transferred), hex, sizeof(hex)), id(s),
                        describe(receive.from, receive.from_length ? *receive.from_length : 0, text, sizeof(text)));
    }
    SetLastError(err);
    return r;
}

using RecvFromFn = int(WINAPI *)(SOCKET, char *, int, int, void *, int *);
RecvFromFn g_orig_recvfrom;
int WINAPI hook_recvfrom(SOCKET s, char *data, int length, int flags, void *from, int *from_length) {
    const int r = g_orig_recvfrom(s, data, length, flags, from, from_length);
    const DWORD err = GetLastError();
    char text[80], hex[48];
    if (r > 0 && allowed())
        g_logger->line("net: recvfrom(%lu) = %d bytes [%s] from %s", id(s), r, hex_prefix(data, r, hex, sizeof(hex)),
                    describe(from, from_length ? *from_length : 0, text, sizeof(text)));
    SetLastError(err);
    return r;
}

template <typename Fn>
void hook(const char *name, Fn detour, Fn *orig) {
    if (MH_CreateHookApi(L"ws2_32", name, reinterpret_cast<void *>(detour), reinterpret_cast<void **>(orig)) != MH_OK)
        g_logger->line("net: could not hook %s", name);
}

}  // namespace

void install(bool write_log, bool reuse_address, bool alias_other_copy) {
    g_logger = &g_log;
    if (!write_log) g_budget = 0;
    if (!write_log && !reuse_address && !alias_other_copy) return;
    InitializeCriticalSection(&g_posted_lock);
    InitializeCriticalSection(&g_alias_lock);
    g_alias_other_copy = alias_other_copy;
    g_reuse_address = reuse_address;
    g_get_sockopt = reinterpret_cast<GetSockOptFn>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("ws2_32.dll"), "getsockopt")));
    hook("socket", &hook_socket, &g_orig_socket);
    hook("bind", &hook_bind, &g_orig_bind);
    hook("setsockopt", &hook_setsockopt, &g_orig_setsockopt);
    hook("sendto", &hook_sendto, &g_orig_sendto);
    hook("connect", &hook_connect, &g_orig_connect);
    hook("listen", &hook_listen, &g_orig_listen);
    hook("WSARecvFrom", &hook_WSARecvFrom, &g_orig_WSARecvFrom);
    hook("WSAGetOverlappedResult", &hook_WSAGetOverlappedResult, &g_orig_WSAGetOverlappedResult);
    hook("recvfrom", &hook_recvfrom, &g_orig_recvfrom);
    MH_EnableHook(MH_ALL_HOOKS);
    g_log.line("net: Winsock calls are hooked (log %s, reuse address %s, other copy aliased %s)", write_log ? "on" : "off",
               reuse_address ? "on" : "off", alias_other_copy ? "on" : "off");
}

}  // namespace dev_tools::network
