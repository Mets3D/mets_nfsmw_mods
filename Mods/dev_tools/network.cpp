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

bool is_announcement(const char *data, int length) { return data && length >= 12 && data[0] == 'g' && data[1] == 'E' && data[2] == 'A'; }

// The whole announcement (384 bytes, mostly zeros) is logged for the first few packets of each kind, to compare what two copies
// send: everything up to the last byte that is not zero, as hex, and the printable characters beside it.
void log_payload(const char *what, const char *data, int length) {
    static volatile LONG shown[2] = {};
    if (!g_logger || !is_announcement(data, length) || data[3] == 0) return;  // type 0 is the search, all zeros
    if (InterlockedIncrement(&shown[what[0] == 's' ? 0 : 1]) > 6) return;
    int last = length - 1;
    while (last > 12 && data[last] == 0) --last;
    last = last < 191 ? last : 191;
    char hex[3 * 192 + 1], text[193];
    size_t n = 0;
    for (int i = 0; i <= last; ++i) {
        n += snprintf(hex + n, sizeof(hex) - n, "%02x ", static_cast<unsigned char>(data[i]));
        text[i] = data[i] >= 32 && data[i] < 127 ? data[i] : '.';
    }
    hex[n] = 0;
    text[last + 1] = 0;
    g_logger->line("net: %s payload (%d bytes, last non-zero byte %d): %s", what, length, last, hex);
    g_logger->line("net:   as text: %s", text);
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
BindFn g_orig_bind;
SetSockOptFn g_orig_setsockopt;

int WINAPI hook_bind(SOCKET s, const void *name, int length) {
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
    const int r = g_orig_sendto(s, data, length, flags, to, to_length);
    const DWORD err = GetLastError();
    if (r > 0 && allowed()) log_payload("sent", data, length);
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
    const int r = g_orig_connect(s, name, length);
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
        if (received && buffers) {
            if (allowed()) log_payload("received", buffers[0].buf, static_cast<int>(*received));
        }
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
        char text[80], hex[48];
        if (allowed())
            g_logger->line("net: received %lu bytes [%s] on %lu from %s", static_cast<unsigned long>(*transferred),
                        hex_prefix(receive.data, static_cast<int>(*transferred), hex, sizeof(hex)), id(s),
                        describe(receive.from, receive.from_length ? *receive.from_length : 0, text, sizeof(text)));
        if (allowed()) log_payload("received", receive.data, static_cast<int>(*transferred));
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

void install() {
    g_logger = &g_log;
    InitializeCriticalSection(&g_posted_lock);
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
    g_log.line("net: Winsock calls are hooked (logging only)");
}

}  // namespace dev_tools::network
