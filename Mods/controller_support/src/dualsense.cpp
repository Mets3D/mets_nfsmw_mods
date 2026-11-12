// DualSense adaptive triggers through DSX (DualSenseX 1.4.x, github.com/Paliverse/DualSenseX). DSX listens on a local UDP port, which
// it writes to C:\Temp\DualSenseX\DualSenseX_PortNumber.txt, for JSON packets that set the triggers of the DualSense it has. The
// game cannot reach the triggers itself: with DSX's Xbox 360 emulation it only sees an Xbox controller. While the player who holds
// that controller (rumble.cpp finds them) is in a running race, the triggers the accelerate and brake commands are on resist more
// the further they are pulled, and the accelerator's vibrates for the kick of a gear shift. Outside a race they go back to normal.
// The packets are sent from the rumble thread (rumble.cpp).
#include <winsock2.h>

#include <cstdio>
#include <cstring>

#include "dualsense.h"
#include "pad.h"

namespace pad::dualsense {
namespace {

const char kPortFile[] = "C:\\Temp\\DualSenseX\\DualSenseX_PortNumber.txt";

// DSX's instruction type 1 is TriggerUpdate, trigger 1 is the left one and 2 the right; mode 0 is Normal, 8 VibrateTrigger
// (intensity 0-255), 12 CustomTriggerValue (see rising_resistance).
struct Settings {  // [dualsensex]
    int use_trigger_rumble = 1;  // ints only: the settings are compared as bytes, which padding would spoil
    int rumble_min = 10, rumble_max = 40;  // the kick's intensity at the Gear Shifts level's lowest and highest
    int accel_resistance = 4, brake_resistance = 2;  // at the end of the pull, 0-8
};
Settings g_set;
bool g_set_loaded = false;

volatile int g_accelerator = -1, g_brake = -1;  // the triggers (0 left, 1 right) the commands are on
volatile float g_shift_level = 0.0f, g_shift_shape = 0.0f;  // the Gear Shifts level, and where the kick is on its decay
volatile bool g_dsx_running = false;
int g_port = 0;  // DSX's, from its file (rumble thread only)

int ini_int(const char *key, int def, int lo, int hi) {
    const int v = static_cast<int>(GetPrivateProfileIntA("dualsensex", key, def, g_ini));
    return v < lo ? lo : v > hi ? hi : v;
}

unsigned long long ini_write_time() {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(g_ini, GetFileExInfoStandard, &data)) return 0;
    return (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

// The feel can be tuned without leaving the game: the ini is read at the start and after it was saved (its time of last change is
// looked at once a second, the file itself is left alone). Read once more a second after a change, in case the save was still going on.
void reload_if_changed() {
    static DWORD next = 0;
    static unsigned long long read_at = 0;
    static int reads_left = 1;
    if (static_cast<LONG>(GetTickCount() - next) < 0) return;
    next = GetTickCount() + 1000;
    const unsigned long long written = ini_write_time();
    if (written != read_at) reads_left = 2;
    read_at = written;
    if (reads_left <= 0) return;
    --reads_left;
    Settings s;
    s.use_trigger_rumble = ini_int("use_trigger_rumble", 1, 0, 1);
    s.rumble_min = ini_int("trigger_rumble_min", s.rumble_min, 0, 255);
    s.rumble_max = ini_int("trigger_rumble_max", s.rumble_max, 0, 255);
    s.accel_resistance = ini_int("accel_trigger_resistance", s.accel_resistance, 0, 8);
    s.brake_resistance = ini_int("brake_trigger_resistance", s.brake_resistance, 0, 8);
    if (g_set_loaded && !memcmp(&s, &g_set, sizeof(s))) return;
    g_set = s;
    g_set_loaded = true;
    log_line("dualsensex: use_trigger_rumble=%d, trigger_rumble_min=%d, trigger_rumble_max=%d, accel_trigger_resistance=%d, brake_trigger_resistance=%d",
             s.use_trigger_rumble, s.rumble_min, s.rumble_max, s.accel_resistance, s.brake_resistance);
}

int read_port() {
    FILE *f = fopen(kPortFile, "r");
    if (!f) return 0;
    char text[16] = {};
    fgets(text, sizeof(text), f);
    fclose(f);
    const int port = atoi(text);
    return port > 0 && port < 65536 ? port : 0;
}

SOCKET g_sock = INVALID_SOCKET;
bool g_refused = false;  // a packet of ours found nobody listening on the port

bool open_socket() {
    static bool broken = false;
    if (g_sock != INVALID_SOCKET) return true;
    if (broken) return false;
    WSADATA data;
    u_long non_blocking = 1;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0 || (g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET ||
        ioctlsocket(g_sock, FIONBIO, &non_blocking) != 0) {
        broken = true;
        log_line("dualsensex: could not open a UDP socket (error %d)", WSAGetLastError());
        return false;
    }
    return true;
}

void send_packet(const char *json) {
    if (!g_port || !open_socket()) return;
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<u_short>(g_port));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(g_sock, json, static_cast<int>(strlen(json)), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
}

// What came back for our packets: Windows refuses one sent to a local port nobody listens on, which the socket's next read reports
// (WSAECONNRESET). Replies (newer versions of DSX send one) are thrown away.
void read_answers() {
    if (g_sock == INVALID_SOCKET) return;
    for (int i = 0; i < 16; ++i) {
        char reply[512];
        if (recvfrom(g_sock, reply, sizeof(reply), 0, nullptr, nullptr) >= 0) continue;
        if (WSAGetLastError() != WSAECONNRESET) break;
        g_refused = true;
    }
}

// DSX is there when its port file names a port and something listens on it: the file stays when DSX is closed, and the program's name
// may differ between versions. Every two seconds the port is read and sent an empty packet (no instructions: DSX does nothing);
// no refusal a fifth of a second later means DSX. A refusal of any packet in between means it is gone. For the game thread's
// takes_shift() as well as our own sending.
void check_dsx() {
    static DWORD next = 0, asked_at = 0;
    static bool asked = false;
    const auto set_running = [](bool running) {
        if (running != g_dsx_running) log_line("dualsensex: DSX %s", running ? "is running" : "is not running (the triggers are left alone)");
        g_dsx_running = running;
    };
    read_answers();
    const DWORD now = GetTickCount();
    if (g_refused) set_running(false);
    else if (asked && now - asked_at >= 200) set_running(true);
    if (g_refused || now - asked_at >= 200) asked = false;
    if (static_cast<LONG>(now - next) < 0) return;
    next = now + 2000;
    g_port = read_port();
    g_refused = false;
    if (!g_port) return set_running(false);
    send_packet("{\"instructions\":[]}");
    asked = true;
    asked_at = now;
}

// One trigger's instruction (controller 0, trigger 1 or 2).
int normal(char *out, size_t n, int trigger) { return snprintf(out, n, "{\"type\":1,\"parameters\":[0,%d,0]}", trigger); }
// A resistance that grows evenly over the pull, from nothing to `strength` (1-8) at its end. The controller's feedback effect (0x21)
// takes a force for each tenth of the pull; DSX's own Resistance mode gives them all the same one, so the effect's bytes go through
// its CustomTriggerValue instead, whose mode 2 ("Rigid A") is the effect's number: the tenths in use (two bytes), then three bits of
// force - 1 for each (four bytes).
int rising_resistance(char *out, size_t n, int trigger, int strength) {
    unsigned zones = 0, forces = 0;
    for (int zone = 0; zone < 10; ++zone) {
        const int force = (strength * (zone + 1) + 5) / 10;
        if (force <= 0) continue;
        zones |= 1u << zone;
        forces |= static_cast<unsigned>(force - 1) << (3 * zone);
    }
    if (!zones) return normal(out, n, trigger);
    return snprintf(out, n, "{\"type\":1,\"parameters\":[0,%d,12,2,%u,%u,%u,%u,%u,%u,0]}", trigger, zones & 0xFF, zones >> 8, forces & 0xFF,
                    (forces >> 8) & 0xFF, (forces >> 16) & 0xFF, forces >> 24);
}
int vibration(char *out, size_t n, int trigger, int intensity) {
    return snprintf(out, n, "{\"type\":1,\"parameters\":[0,%d,8,%d]}", trigger, intensity);
}

}  // namespace

void set_triggers(int accelerator, int brake) {
    g_accelerator = accelerator;
    g_brake = brake;
}

void set_shift(float level, float shape) {
    g_shift_level = level;
    g_shift_shape = shape;
}

bool takes_shift() { return g_dsx_running && g_set.use_trigger_rumble && g_accelerator >= 0; }

void update(bool racing, bool previewing) {
    static bool on = false, announced = false;
    static char last[320] = {};
    static DWORD last_send = 0;
    static int last_shake = 0;
    reload_if_changed();
    check_dsx();
    const bool want = (racing || previewing) && g_dsx_running;
    if (!want && !on) return;
    const int accelerator = g_accelerator, brake = g_brake;
    // The kick starts at the intensity the Gear Shifts level gives (the lowest level's at [min], the highest's at [max]; Off gives none)
    // and decays with the kick's shape; in steps, so that it is not a new packet every few milliseconds.
    const float level = g_shift_level, shape = g_shift_shape;
    int shake = 0;
    if (takes_shift() && level > 0.0f && shape > 0.0f) {
        const float peak = static_cast<float>(g_set.rumble_min) + static_cast<float>(g_set.rumble_max - g_set.rumble_min) * level;
        shake = static_cast<int>(peak * shape / 2.0f + 0.5f) * 2;
    }
    char json[320];
    int n = snprintf(json, sizeof(json), "{\"instructions\":[");
    for (int trigger = 0; trigger < 2; ++trigger) {  // 0 left (DSX's 1), 1 right (DSX's 2)
        if (trigger) json[n++] = ',';
        char *at = json + n;
        const size_t room = sizeof(json) - n;
        if (!want) n += normal(at, room, trigger + 1);
        else if (trigger == accelerator && shake > 0) n += vibration(at, room, trigger + 1, shake);
        else if (!racing) n += normal(at, room, trigger + 1);  // only the sample
        else if (trigger == accelerator) n += rising_resistance(at, room, trigger + 1, g_set.accel_resistance);
        else if (trigger == brake) n += rising_resistance(at, room, trigger + 1, g_set.brake_resistance);
        else n += normal(at, room, trigger + 1);
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    // A change goes out at once when a race starts or ends or a kick begins, otherwise at most every 40 ms; a steady state is sent
    // again every second, as DSX may have been started after the race did.
    const DWORD now = GetTickCount();
    const bool changed = on != want || strcmp(json, last) != 0;
    const bool kick_begins = shake > 0 && last_shake == 0;
    last_shake = shake;
    if ((changed && (on != want || kick_begins || now - last_send >= 40)) || (want && now - last_send >= 1000)) {
        send_packet(json);
        strcpy(last, json);
        last_send = now;
        if (racing && want && !announced) {
            announced = true;
            log_line("dualsensex: triggers on for the race (accelerator on trigger %d, brake on trigger %d; 0 left, 1 right, -1 none)", accelerator, brake);
        }
        on = want;
    }
}

}  // namespace pad::dualsense
