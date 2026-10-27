// A smackable is a prop that becomes a physics object when something drives near it. Until then it is a trigger in the world's collision assets (WTrigger, in the
// grid WTriggerManager::GetIntersectingTriggers searches) with an ESpawnSmackable event (0xEB626F77) whose data holds the prop's scenery model handle; the model
// (IModel, found by that handle) has the `smackable` attributes: MASS, ... The game's own query is TestAllSmackablesForWoosh (sub_4FA1A0), which this follows.
// After a hit the model has a simable and the trigger is disabled (bit 0 of its flags); the trigger stays where the prop stood, the model's transform moves.
//
// The game's steering around things (WRoadNav::HolePunchAvoidables) takes its bodies from WRoadNav::FetchAvoidables, which only ever gives it the neighbouring cars.
// For the improved AI's nav we add a stand-in body (an IBody: transform, velocity, dimensions; no other interface, so the game treats it as neither traffic nor a
// racer) for each heavy prop around the car, and the game's own corridor cutting steers around it as it does around a car.
#include "obstacles.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include "ai_control.h"
#include "hooks.h"
#include "improved_ai_opponents.h"
#include "car_stats.h"
#include "lines.h"
#include "nav_cookie.h"
#include "settings.h"

namespace improved_ai_opponents::obstacles {

namespace {

constexpr uintptr_t kTriggerManager = 0x9B3904, kGetIntersectingTriggers = 0x784C50, kHasEvent = 0x766930, kKeyedSearch = 0x5D51F0, kModelTable = 0x930514,
                    kModelCount = 0x93051C, kFastMem = 0x925B30, kFastMemFree = 0x5D0370, kGetAttributePointer = 0x454810, kFetchAvoidables = 0x775A20, kPureCall = 0x7C5200;
constexpr unsigned kSpawnSmackable = 0xEB626F77, kMass = 0x4A56503D;
constexpr unsigned kNavAiVehicle = 0x84, kAiVehicleInterface = 0x4C;  // WRoadNav::pAIVehicle is an AIVehicle; its IVehicleAI part is 0x4C into it
constexpr float kRadius = 100.0f;  // how far around the car the props are looked for
constexpr float kMinMass = 200.0f;  // kg: lighter props are driven through (chairs, bins, hydrants 50 to 100; the posts, booths and barrels 200 to 700)
constexpr int kMaxStandIns = 40, kMaxBoxes = 8;  // (a prop has several collision boxes: a pole and a sign on it)

// Whether steering around a prop is worth it (worth_avoiding): a swerve costs speed too, so one is only tried when it is likely to work.
constexpr float kCarHalfWidth = 1.0f, kCarFront = 2.5f;  // m
constexpr float kSideMargin = 0.4f, kWallMargin = 0.5f;  // m: left between the car and the prop, and between the car and the edge of the road
constexpr float kReaction = 0.4f;                        // s before the steering takes effect
constexpr float kLateralShare = 0.5f;                    // of the sideways acceleration the tires hold that a swerve uses
constexpr float kShiftLight = 1.5f, kShiftHeavy = 3.0f;  // m: the most the car swerves for a prop of the lightest weight that is avoided, and for one of kHeavyMass or more
constexpr float kHeavyMass = 700.0f;                     // kg
constexpr float kCommittedShift = 1.5f;                  // a prop that is being steered around stays so up to this many times the shift

// IModel vtable slots (the decomp's list, one later: the first slot is IUnknown's destructor).
constexpr unsigned kModelIsHidden = 0x24, kModelGetLinearVelocity = 0x34, kModelGetTransform = 0x3C, kModelGetBounds = 0x40, kModelGetAttributes = 0x44;

struct TriggerList {  // WTriggerList: UTL::Std::vector<WTrigger *>
    int allocator;
    void **begin, **end, **capacity;
};

struct Trigger {  // CARP::Trigger, 0x40 bytes
    float row0[4];
    unsigned type_shape_flags;
    float height;
    void *events;
    unsigned short stamp, fingerprint;
    float row2[4];
    float position_radius[4];
};

// One collision box of a prop, in the prop's own axes: the centre, the box's three axes and its half sizes along them.
struct Box {
    float centre[3];
    float axis[3][3];
    float half[3];
};

struct Prop {
    void *model;
    float distance;  // from the car, along the ground
    float mass;
    std::vector<Box> boxes;
    bool standing;  // not knocked down: its trigger is still enabled
};

template <typename T> T slot(void *object, unsigned offset) { return reinterpret_cast<T>((*static_cast<void ***>(object))[offset / 4]); }

// Whether a model can be called: the track streamer takes scenery away while the game runs (with the free camera far from the car, say), and an object being taken apart
// has the pure virtual stub in its vtable (a call to it ends the game with R6025).
bool alive(const void *model) {
    if (!model || IsBadReadPtr(model, 4)) return false;
    void *const *vtable = *static_cast<void *const *const *>(model);
    if (!vtable || IsBadReadPtr(vtable, 0x48)) return false;
    for (unsigned offset : {kModelIsHidden, kModelGetLinearVelocity, kModelGetTransform, kModelGetBounds, kModelGetAttributes}) {
        void *function = vtable[offset / 4];
        if (!function || function == reinterpret_cast<void *>(kPureCall)) return false;
    }
    return true;
}

const void *attribute(const void *attributes, unsigned key) {
    const void *value = reinterpret_cast<const void *(__thiscall *)(const void *, unsigned, unsigned)>(kGetAttributePointer)(attributes, key, 0);
    return value && !IsBadReadPtr(value, 4) ? value : nullptr;
}

float attribute_float(const void *attributes, unsigned key) {
    const void *value = attribute(attributes, key);
    return value ? *static_cast<const float *>(value) : -1.0f;
}

// The collision boxes of a prop: what the game's physics collides with. The root of the prop's collision geometry (CollisionGeometry::Bounds, 0x30 bytes: orientation
// 4 shorts /32767 at 0, position 3 shorts in mm at 8, flags at 0xE, half size 3 shorts in mm at 0x10, child count at 0x16, first child's index at 0x1E, collection at 0x2C)
// encloses the whole prop (the sign on a pole, the pole's foot); its children, in the collection's array of nodes (at +0x10 of it, as Collection::AddTo 0x672050 walks it),
// are the pieces the game collides with. A node without children is a box itself. What the physics builds from a node (RigidBody::AddCollisionPrimitive): nothing if it
// is disabled (1 << 0), a box (1 << 6) or sphere (1 << 7) if it collides with the world (1 << 1), objects (1 << 2) or the ground (1 << 3), else nothing. A car is an object,
// so the pieces that matter here are the ones that collide with objects.
void collect_boxes(const unsigned char *node, int depth, std::vector<Box> &boxes) {
    if (!node || IsBadReadPtr(node, 0x30) || boxes.size() >= kMaxBoxes) return;
    const unsigned short flags = *reinterpret_cast<const unsigned short *>(node + 0xE);
    if (flags & 1) return;  // kBounds_Disabled
    const int children = node[0x16], first = *reinterpret_cast<const short *>(node + 0x1E);
    const unsigned char *collection = *reinterpret_cast<const unsigned char *const *>(node + 0x2C);
    if (children > 0 && first >= 0 && depth < 3 && collection && !IsBadReadPtr(collection, 0x10 + (first + children) * 0x30)) {
        for (int i = 0; i < children; ++i) collect_boxes(collection + 0x10 + (first + i) * 0x30, depth + 1, boxes);
        return;
    }
    if (!(flags & 4) || !(flags & 0xC0)) return;  // not a box or sphere that collides with objects
    Box b = {};
    const short *position = reinterpret_cast<const short *>(node + 8), *half = reinterpret_cast<const short *>(node + 0x10), *q = reinterpret_cast<const short *>(node);
    for (int i = 0; i < 3; ++i) {
        b.centre[i] = position[i] / 1000.0f;
        b.half[i] = (flags & 0x80) ? *reinterpret_cast<const float *>(node + 0x20) : half[i] / 1000.0f;  // kBounds_Sphere: the radius
    }
    const float x = q[0] / 32767.0f, y = q[1] / 32767.0f, z = q[2] / 32767.0f, w = q[3] / 32767.0f;
    if (x == 0 && y == 0 && z == 0 && w == 0) {
        b.axis[0][0] = b.axis[1][1] = b.axis[2][2] = 1.0f;
    } else {  // the rotation matrix of the quaternion: its columns are the box's axes
        const float n = 1.0f / std::sqrt(x * x + y * y + z * z + w * w), qx = x * n, qy = y * n, qz = z * n, qw = w * n;
        const float c0[3] = {1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw)};
        const float c1[3] = {2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw)};
        const float c2[3] = {2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy)};
        std::copy(c0, c0 + 3, b.axis[0]);
        std::copy(c1, c1 + 3, b.axis[1]);
        std::copy(c2, c2 + 3, b.axis[2]);
    }
    boxes.push_back(b);
}

// The smackables within `radius` of the car, nearest first (all of them: the caller picks). Valid until the models go (the next frame).
std::vector<Prop> props_around(const float *car, float radius) {
    std::vector<Prop> props;
    void *manager = *reinterpret_cast<void **>(kTriggerManager);
    if (!manager) return props;
    TriggerList list = {};
    reinterpret_cast<void(__thiscall *)(void *, const float *, float, TriggerList *)>(kGetIntersectingTriggers)(manager, car, radius, &list);
    const unsigned char *table = *reinterpret_cast<const unsigned char **>(kModelTable);
    const int count = *reinterpret_cast<const int *>(kModelCount);
    for (void **it = list.begin; it && it != list.end; ++it) {
        Trigger *trigger = static_cast<Trigger *>(*it);
        const unsigned char *event = nullptr;
        if (!reinterpret_cast<bool(__thiscall *)(void *, unsigned, const unsigned char **)>(kHasEvent)(trigger, kSpawnSmackable, &event) || !event) continue;
        const unsigned char *data = event + *reinterpret_cast<const int *>(event + 8);  // ESpawnSmackable::StaticData
        const float *event_at = reinterpret_cast<const float *>(data + 4);
        const unsigned scenery = *reinterpret_cast<const unsigned *>(data + 0x10);
        const unsigned char *node = scenery ? reinterpret_cast<const unsigned char *(__cdecl *)(const void *, const void *, unsigned)>(kKeyedSearch)(table, table + count * 8, scenery) : nullptr;
        void *model = node ? *reinterpret_cast<void *const *>(node + 4) : nullptr;
        if (!alive(model) || slot<bool(__thiscall *)(void *)>(model, kModelIsHidden)(model)) continue;  // gone, or not in the world (a prop the race does not use)

        Prop p = {};
        p.model = model;
        p.distance = std::hypot(event_at[0] - car[0], event_at[2] - car[2]);
        p.standing = (trigger->type_shape_flags >> 8 & 1) != 0;
        const void *attributes = slot<const void *(__thiscall *)(void *)>(model, kModelGetAttributes)(model);
        p.mass = attributes ? attribute_float(attributes, kMass) : -1.0f;
        collect_boxes(slot<const unsigned char *(__thiscall *)(void *)>(model, kModelGetBounds)(model), 0, p.boxes);
        props.push_back(p);
    }
    if (list.begin) reinterpret_cast<void(__thiscall *)(void *, void *, unsigned, void *)>(kFastMemFree)(reinterpret_cast<void *>(kFastMem), list.begin, (list.capacity - list.begin) * 4, nullptr);
    std::sort(props.begin(), props.end(), [](const Prop &a, const Prop &b) { return a.distance < b.distance; });
    return props;
}

// A stand-in for the IBody of a prop (all HolePunchAvoidables asks of a body: IBody's vtable, and a COM object with no other interface to find).
struct StandIn {
    void **vtable;
    void *com_object;
    void *model;
    Box box;
    bool standing;
};
void *g_no_interfaces[4];  // an empty Object::_mInterfaces: QueryInterface finds nothing

// A box of a prop in the world: its centre, its three axes (the half sizes along them are the box's), from the model's transform (rows: right, up, forward, position).
struct WorldBox {
    float centre[3], axis[3][3];
};
WorldBox world_box(const float *m, const Box &b) {
    WorldBox w;
    const auto to_world = [&](const float *l, float *out) {
        for (int i = 0; i < 3; ++i) out[i] = l[0] * m[i] + l[1] * m[4 + i] + l[2] * m[8 + i];
    };
    to_world(b.centre, w.centre);
    for (int i = 0; i < 3; ++i) {
        w.centre[i] += m[12 + i];
        to_world(b.axis[i], w.axis[i]);
    }
    return w;
}

// Whether the car should try to steer around a box of a prop: only when it is in the car's way, there is road to go round it on (not into the edge), the swerve is a small
// one (a heavier prop is worth a bigger one) and the car has the time for it, which its tires' grip allows. Otherwise the speed that the swerve costs is worse than what
// the hit costs. `committed`: it is being steered around already, which goes on (a swerve that is stopped half way is the worst) while the shift stays small.
bool worth_avoiding(const nav_cookie::Trail &trail, const float *car, const float *velocity, const WorldBox &w, const Box &b, float mass, const car_stats::Stats &stats, bool committed) {
    const float speed = std::hypot(velocity[0], velocity[2]);
    const int at = trail.nearest(w.centre[0], w.centre[2]), here = trail.nearest(car[0], car[2]);
    if (speed < 1.0f || at < 0 || here < 0) return false;
    const nav_cookie::Cookie &road_at = trail[at];
    const float direction[2] = {velocity[0] / speed, velocity[2] / speed}, across[2] = {road_at.forward[1], -road_at.forward[0]};
    float half_across = 0.0f, half_along = 0.0f;  // what the box covers across the road and along the car's way
    for (int i = 0; i < 3; ++i) {
        half_across += std::fabs(across[0] * w.axis[i][0] + across[1] * w.axis[i][2]) * b.half[i];
        half_along += std::fabs(direction[0] * w.axis[i][0] + direction[1] * w.axis[i][2]) * b.half[i];
    }
    const float ahead = (w.centre[0] - car[0]) * direction[0] + (w.centre[2] - car[2]) * direction[1] - half_along - kCarFront;
    if (ahead <= 0.0f) return false;
    const float seconds = ahead / speed;
    const float box_at = road_at.across(w.centre[0], w.centre[2]), car_at = trail[here].across(car[0], car[2]);
    const float clearance = kCarHalfWidth + kSideMargin;
    const float low = box_at - half_across - clearance, high = box_at + half_across + clearance;  // the car's centre has to be outside of these
    if (car_at <= low || car_at >= high) return false;                                              // not in the way
    float shift = 1.0e9f;
    if (low >= road_at.left_offset + kCarHalfWidth + kWallMargin) shift = std::min(shift, car_at - low);
    if (high <= road_at.right_offset - kCarHalfWidth - kWallMargin) shift = std::min(shift, high - car_at);
    if (shift > 1.0e8f) return false;  // no road to go round it on
    const float heaviness = std::clamp((mass - kMinMass) / (kHeavyMass - kMinMass), 0.0f, 1.0f);
    const float most = kShiftLight + (kShiftHeavy - kShiftLight) * heaviness;
    if (committed) return shift <= most * kCommittedShift;
    const float lateral = kLateralShare * (stats.lateral > 0.0f ? stats.lateral : 8.0f);
    const float time = std::max(seconds - kReaction, 0.0f);
    return shift <= std::min(most, 0.5f * lateral * time * time);
}

void __fastcall stand_in_destructor(StandIn *, void *) {}
// HolePunchAvoidables reads a body's position, its right and forward axes and its half sizes across and along them, in the ground plane. So the stand-in's axes are the
// prop's heading (its forward axis on the ground) and what the box covers across and along them, whichever way the box is turned.
bool stand_in_frame(const StandIn *self, float *matrix, float *dimension) {
    std::fill(matrix, matrix + 16, 0.0f);
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
    std::fill(dimension, dimension + 3, 0.0f);
    if (!alive(self->model)) {
        matrix[13] = 1.0e6f;  // gone: far away (HolePunchAvoidables skips what is 5 m above or below the road)
        return false;
    }
    float m[16];
    slot<void(__thiscall *)(void *, float *)>(self->model, kModelGetTransform)(self->model, m);
    const WorldBox w = world_box(m, self->box);
    float forward[2] = {m[8], m[10]};
    const float length = std::hypot(forward[0], forward[1]);
    if (length < 1e-3f) {
        forward[0] = 0.0f;
        forward[1] = 1.0f;
    } else {
        forward[0] /= length;
        forward[1] /= length;
    }
    const float right[2] = {forward[1], -forward[0]};
    for (int i = 0; i < 3; ++i) {
        dimension[0] += std::fabs(right[0] * w.axis[i][0] + right[1] * w.axis[i][2]) * self->box.half[i];
        dimension[1] += std::fabs(w.axis[i][1]) * self->box.half[i];
        dimension[2] += std::fabs(forward[0] * w.axis[i][0] + forward[1] * w.axis[i][2]) * self->box.half[i];
    }
    matrix[0] = right[0];
    matrix[2] = right[1];
    matrix[8] = forward[0];
    matrix[10] = forward[1];
    std::copy(w.centre, w.centre + 3, matrix + 12);
    return true;
}
void __fastcall stand_in_transform(StandIn *self, void *, float *matrix) {
    float dimension[3];
    stand_in_frame(self, matrix, dimension);
}
void __fastcall stand_in_velocity(StandIn *self, void *, float *velocity) {
    velocity[0] = velocity[1] = velocity[2] = 0.0f;
    if (!self->standing && alive(self->model)) slot<void(__thiscall *)(void *, float *)>(self->model, kModelGetLinearVelocity)(self->model, velocity);
}
void __fastcall stand_in_angular_velocity(StandIn *, void *, float *velocity) { velocity[0] = velocity[1] = velocity[2] = 0.0f; }
void __fastcall stand_in_dimension(StandIn *self, void *, float *dimension) {
    float matrix[16];
    stand_in_frame(self, matrix, dimension);
}
const void *__fastcall stand_in_attributes(StandIn *self, void *) { return alive(self->model) ? slot<const void *(__thiscall *)(void *)>(self->model, kModelGetAttributes)(self->model) : nullptr; }
unsigned __fastcall stand_in_world_id(StandIn *, void *) { return 0; }

void *g_stand_in_vtable[7] = {reinterpret_cast<void *>(stand_in_destructor), reinterpret_cast<void *>(stand_in_transform), reinterpret_cast<void *>(stand_in_velocity),
                              reinterpret_cast<void *>(stand_in_angular_velocity), reinterpret_cast<void *>(stand_in_dimension), reinterpret_cast<void *>(stand_in_attributes),
                              reinterpret_cast<void *>(stand_in_world_id)};

StandIn g_stand_ins[kPlayers][kMaxStandIns];  // per player
int g_stand_in_count[kPlayers];
DWORD g_stand_in_frame[kPlayers] = {~0u, ~0u, ~0u, ~0u};
std::vector<std::pair<void *, int>> g_steered_around[kPlayers];  // the (model, box) pairs of the last frame's stand-ins: those are being steered around

using FetchFn = int(__fastcall *)(void *, void *, void **, int);
FetchFn g_orig_fetch = nullptr;

int __fastcall hook_fetch_avoidables(void *nav, void *edx, void **avoidables, int size) {
    int count = g_orig_fetch(nav, edx, avoidables, size);
    const int player = control::improved_player(nav);
    unsigned char *vehicle = *reinterpret_cast<unsigned char **>(static_cast<unsigned char *>(nav) + kNavAiVehicle);
    void *ai = vehicle ? vehicle + kAiVehicleInterface : nullptr;
    const float *car = player >= 0 && ai && settings::player(player).avoid_props ? control::car_position(ai) : nullptr;
    float velocity[3];
    if (!car || !control::car_velocity(player, velocity)) return count;
    const DWORD frame = GetTickCount() / 16;
    if (g_stand_in_frame[player] != frame) {  // the props are looked for once a frame
        g_stand_in_frame[player] = frame;
        g_stand_in_count[player] = 0;
        const nav_cookie::Trail trail(nav);
        const car_stats::Stats &stats = car_stats::of(player);
        std::vector<std::pair<void *, int>> steered;
        for (const Prop &p : props_around(car, kRadius)) {
            if (!p.model || p.mass < kMinMass) continue;
            float m[16];
            slot<void(__thiscall *)(void *, float *)>(p.model, kModelGetTransform)(p.model, m);
            for (size_t i = 0; i < p.boxes.size(); ++i) {
                const bool committed = std::find(g_steered_around[player].begin(), g_steered_around[player].end(), std::make_pair(p.model, static_cast<int>(i))) != g_steered_around[player].end();
                if (g_stand_in_count[player] >= kMaxStandIns || !worth_avoiding(trail, car, velocity, world_box(m, p.boxes[i]), p.boxes[i], p.mass, stats, committed)) continue;
                g_stand_ins[player][g_stand_in_count[player]++] = {g_stand_in_vtable, g_no_interfaces, p.model, p.boxes[i], p.standing};
                steered.emplace_back(p.model, static_cast<int>(i));
            }
        }
        g_steered_around[player] = steered;
    }
    for (int i = 0; i < g_stand_in_count[player] && count < size; ++i) avoidables[count++] = reinterpret_cast<void *>(&g_stand_ins[player][i]);
    return count;
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"ai_obstacles", "WRoadNav::FetchAvoidables", kFetchAvoidables, {0x8B, 0x81, 0x84, 0x00, 0x00, 0x00, 0x83}, false, nfsmw::detour(&hook_fetch_avoidables), nfsmw::orig(&g_orig_fetch)},
    };
    nfsmw::install_hooks(hooks, std::size(hooks), g_log, g_ini, "ai obstacles");
}

// The props' collision boxes (every piece the game's physics collides with, not the box around the whole prop), as the avoidance sees them. Orange-red: steered around
// (standing, heavy). Yellow: the same, knocked down. Purple: heavy, but not steered around (not in the car's way, or no time or room to get round it). Grey-blue: too
// light. Each prop with its mass in kg.
void add_boxes(canvas::Lines &lines, const float *car, int player, const void *nav) {
    constexpr DWORD kAvoided = 0xFFFF5020, kKnocked = 0xFFFFD020, kSkipped = 0xFFB050D0, kLight = 0xFF6080A0;
    const nav_cookie::Trail trail(nav);
    float velocity[3] = {0, 0, 0};
    control::car_velocity(player, velocity);
    const car_stats::Stats &stats = car_stats::of(player);
    for (const Prop &p : props_around(car, 80.0f)) {
        if (!p.model || !alive(p.model)) continue;
        float m[16];
        slot<void(__thiscall *)(void *, float *)>(p.model, kModelGetTransform)(p.model, m);
        const bool heavy = p.mass >= kMinMass;
        float top_y = m[13];
        DWORD label_color = kLight;
        for (const Box &b : p.boxes) {
            const WorldBox w = world_box(m, b);
            const DWORD color = !heavy ? kLight : !worth_avoiding(trail, car, velocity, w, b, p.mass, stats, false) ? kSkipped : p.standing ? kAvoided : kKnocked;
            if (label_color != kAvoided) label_color = color;
            Vec3 corner[8];
            for (int i = 0; i < 8; ++i) {
                float at[3] = {w.centre[0], w.centre[1], w.centre[2]};
                for (int axis = 0; axis < 3; ++axis) {
                    const float sign = (i >> axis & 1) ? 1.0f : -1.0f;
                    for (int k = 0; k < 3; ++k) at[k] += sign * b.half[axis] * w.axis[axis][k];
                }
                corner[i] = {at[0], at[1], at[2]};
                top_y = std::max(top_y, at[1]);
            }
            for (int i = 0; i < 8; ++i)
                for (int bit = 1; bit < 8; bit <<= 1)
                    if (!(i & bit)) lines.add(corner[i], corner[i | bit], color);
        }
        if (p.distance > 40.0f || p.mass < 0.0f) continue;
        const canvas::Clip top = lines.project({m[12], top_y + 0.3f, m[14]});
        if (top.w < 0.5f) continue;
        float x, y;
        lines.to_pixels(top, &x, &y);
        char text[16];
        snprintf(text, sizeof(text), "%.0f", p.mass);
        lines.add_text(x - lines.text_width(text, 2) * 0.5f, y - 14.0f, text, 2, label_color);
    }
}

}  // namespace improved_ai_opponents::obstacles
