// A hint of our own in a menu's hint bar ("2 Help", "2 Select Car"): a clone of the layout's own "1 Defaults" hint (FEGroup::Clone), one hint to the
// right, with its own label, key picture and click message. Used by custom_menus (Options pages) and split_screen_support (the race options). Header only.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include "nfsmw_util.h"

namespace nfsmw::fe_hint {

// FEObject / FEGroup / FEPackage members (PC).
namespace fe {
constexpr unsigned kNext = 0x04;          // FEMinNode: list link (lists of objects, responses, scripts)
constexpr unsigned kGuid = 0x0C;
constexpr unsigned kNameHash = 0x10;
constexpr unsigned kType = 0x18;          // kImage, kString, kGroup
constexpr unsigned kFlags = 0x1C;
constexpr unsigned kHandle = 0x24;        // images: texture hash
constexpr unsigned kData = 0x2C;          // FEObjData*: colour (alpha at +0xC), pivot, position (x at +0x1C), ...
constexpr unsigned kDataSize = 0x30;
constexpr unsigned kResponseCount = 0x38;  // message responses; node: MsgID +0xC, count +0x10,
constexpr unsigned kResponseHead = 0x3C;   //   array +0x14 of {ID, Param, Target}
constexpr unsigned kScriptCount = 0x48;    // scripts; FEScript: events count +0x24, array +0x28 of
constexpr unsigned kScriptHead = 0x4C;     //   {EventID, Target, Time}, ID +0x30
constexpr unsigned kCurrentScript = 0x54;
constexpr unsigned kCached = 0x58;         // render object built from the data
constexpr unsigned kChildCount = 0x60;     // FEGroup
constexpr unsigned kChildHead = 0x64;
constexpr unsigned kPackageObjects = 0x4C;  // FEPackage: head of its top-level objects

constexpr unsigned kDataAlpha = 0x0C;
constexpr unsigned kDataPosX = 0x1C;

constexpr int kImage = 1, kString = 2, kGroup = 5;
constexpr unsigned kMouseObject = 1u << 17;  // hit-box: the package tracks the mouse over it
constexpr unsigned kDirtyColor = 1u << 23, kDirtyTransform = 1u << 24, kDirty = 1u << 25;
}  // namespace fe

// The hint bars: top-level groups of {label string, clickable hit-box group, key image}. The option screens' PC hints (Pause_Options.fng and others, the race
// options' Track_Options.fng too) and the main menu's option layouts (Options.fng, OptionsPCDisplay.fng) name them differently.
constexpr unsigned kPcDefaultsHint = 0x3FC72F9D, kPcAcceptHint = 0x9502D148;      // "1 Defaults", "Enter Accept"
constexpr unsigned kMainDefaultsHint = 0xD6463100, kMainAcceptHint = 0x73DCB662;

constexpr unsigned kButton5Msg = 0xC519BFC4;  // __PAD_BUTTON5__: the 1 key (Defaults)
constexpr unsigned kEventSetButton = 0x1B3909AA;  // script event that moves the menu cursor
constexpr unsigned kScriptHighlight = 0x249DB7B7, kScriptUnhighlight = 0x7AB5521A;
constexpr unsigned kPcKey1Texture = 0x68A29A8A, kPcKey2Texture = 0x68A29A8B, kPcKey3Texture = 0x68A29A8C, kPcKey4Texture = 0x68A29A8D;
constexpr unsigned kCircleTexture = 0x54A68051;  // drawn as the 1 key on PC

namespace game {
template <typename Fn>
Fn at(uintptr_t addr) { return reinterpret_cast<Fn>(addr); }
#define FE_HINT_THISCALL __attribute__((thiscall))
const auto FEPrintf = at<int (*)(void *string, const char *fmt, ...)>(0x515D70);
const auto FEngSetTextureHash = at<void (*)(void *image, unsigned hash)>(0x515B90);
const auto FEngFindGroup = at<void *(*)(const char *pkg, unsigned object)>(0x524880);
const auto FEngGetCenter = at<void (*)(void *object, float *x, float *y)>(0x524EE0);
const auto FindPackage = at<void *(FE_HINT_THISCALL *)(void *feng, const char *pkg)>(0x516AF0);  // cFEng::FindPackage
const auto GroupClone = at<void *(FE_HINT_THISCALL *)(void *group, int reference)>(0x5C49C0);
const auto GroupAddObject = at<void(FE_HINT_THISCALL *)(void *group, void *object)>(0x5B5420);
const auto PackageAddObject = at<void(FE_HINT_THISCALL *)(void *pkg, void *object)>(0x50DB90);
const auto BuildMouseObjectStateList = at<void(FE_HINT_THISCALL *)(void *pkg)>(0x5B88F0);
const auto SetScript = at<void(FE_HINT_THISCALL *)(void *object, unsigned id, int force)>(0x5BC160);  // FEObject::SetScript
constexpr uintptr_t kFEng = 0x91CADC;  // *kFEng is the cFEng
#undef FE_HINT_THISCALL
}  // namespace game

inline void *find_parent(void *container, bool is_pkg, void *target, bool *parent_is_pkg) {
    void *n = field<void *>(container, is_pkg ? fe::kPackageObjects : fe::kChildHead);
    for (int i = 0; n && i < 1024; ++i) {
        if (n == target) {
            *parent_is_pkg = is_pkg;
            return container;
        }
        if (field<int>(n, fe::kType) == fe::kGroup)
            if (void *r = find_parent(n, false, target, parent_is_pkg)) return r;
        n = field<void *>(n, fe::kNext);
    }
    return nullptr;
}

inline bool is_key_glyph(unsigned tex) {
    return tex == kPcKey1Texture || tex == kPcKey4Texture || tex == kCircleTexture || tex == kPcKey2Texture || tex == kPcKey3Texture;
}

// What a clone needs from its original: FEObject's copy constructor leaves the cached render object holding garbage (the renderer then skips it, and
// unloading the package crashes in FERenderObject::Clear), and the object data (colour, position, size, UVs) blank (position 0,0,0 is never drawn).
// Returns false when the data could not be copied.
inline bool copy_object_state(void *orig, void *copy) {
    field<void *>(copy, fe::kCached) = nullptr;
    void *od = field<void *>(orig, fe::kData);
    void *cd = field<void *>(copy, fe::kData);
    const unsigned osz = field<unsigned>(orig, fe::kDataSize), csz = field<unsigned>(copy, fe::kDataSize);
    const unsigned n = osz < csz ? osz : csz;
    const bool copied = od && cd && od != cd && n >= 0x44 && n <= 0x400;
    if (copied) memcpy(cd, od, n);
    field<unsigned>(copy, fe::kFlags) |= fe::kDirtyColor | fe::kDirtyTransform | fe::kDirty;
    return copied;
}

inline unsigned script_id(void *obj) {
    void *sc = field<void *>(obj, fe::kCurrentScript);
    return sc ? field<unsigned>(sc, 0x30) : 0;
}

inline bool responds_to(void *obj, unsigned msg) {
    void *r = field<void *>(obj, fe::kResponseHead);
    for (unsigned i = 0, n = field<unsigned>(obj, fe::kResponseCount); r && i < n && i < 32; ++i, r = field<void *>(r, fe::kNext))
        if (field<unsigned>(r, 0xC) == msg) return true;
    return false;
}

struct Look {
    const char *label;     // the hint's text
    unsigned key_texture;  // the key picture (kPcKey2Texture: the 2 key; mets_controller_support shows the controller's button for it)
    unsigned click_msg;    // what a click on the hint posts to the screen
    unsigned name_base;    // the clone's name hash; its objects get name_base + 0x100 + i
    unsigned guid_base;    // the GUIDs of its objects
};

class Hint {
  public:
    // Adds the hint to the screen whose package is `pkg_name`, one hint to the right of the Defaults hint. Returns false when the layout has no Defaults
    // hint (or the clone failed); `why` says what went wrong.
    bool add(void *screen, const char *pkg_name, const Look &look, const char **why) {
        clear();
        look_ = look;
        void *defaults = game::FEngFindGroup(pkg_name, kPcDefaultsHint);
        void *accept = game::FEngFindGroup(pkg_name, kPcAcceptHint);
        if (!defaults) {
            defaults = game::FEngFindGroup(pkg_name, kMainDefaultsHint);
            accept = game::FEngFindGroup(pkg_name, kMainAcceptHint);
        }
        if (!defaults) return fail(why, "the layout has no Defaults hint");
        void *pkg = game::FindPackage(*reinterpret_cast<void **>(game::kFEng), pkg_name);
        bool parent_is_pkg = false;
        void *parent = pkg ? find_parent(pkg, true, defaults, &parent_is_pkg) : nullptr;
        if (!parent) return fail(why, "the Defaults hint's parent was not found");
        void *clone = game::GroupClone(defaults, 0);
        if (!clone) return fail(why, "FEGroup::Clone returned null");
        prepare(defaults, clone, 0, true);
        retarget_responses();
        retarget_script_events();

        // One hint further along, with the same gap as between the Accept and Defaults hints. Only x is moved: FEngSetCenter would also reset z (depth),
        // which hides the object.
        float dx = 0, dy = 0, ax = 0, ay = 0;
        game::FEngGetCenter(defaults, &dx, &dy);
        float spacing = 120.0f;
        if (accept) {
            game::FEngGetCenter(accept, &ax, &ay);
            const float gap = dx - ax;
            if (gap > 1.0f || gap < -1.0f) spacing = gap;
        }
        if (void *data = field<void *>(clone, fe::kData)) field<float>(data, fe::kDataPosX) += spacing;

        if (parent_is_pkg) game::PackageAddObject(parent, clone);
        else game::GroupAddObject(parent, clone);
        game::BuildMouseObjectStateList(pkg);  // the package's list of hit-boxes, so the clone's is clickable
        screen_ = screen;
        clone_ = clone;
        return true;
    }

    // Each frame: run the same script as the original (appear, fade out, ...) and copy its alpha, so the clone looks and animates like the Defaults hint.
    // Hover is the exception: the hit-box and the label's highlight are the clone's own.
    void sync(void *screen) {
        if (screen_ != screen || !clone_) return;
        for (int i = 0; i < n_; ++i) {
            const Pair &p = pair_[i];
            if (!p.mirrored) continue;
            const unsigned id = script_id(p.orig);
            if (field<int>(p.copy, fe::kType) == fe::kString && (id == kScriptHighlight || id == kScriptUnhighlight)) continue;
            if (id && id != script_id(p.copy)) game::SetScript(p.copy, id, 1);
            void *od = field<void *>(p.orig, fe::kData);
            void *cd = field<void *>(p.copy, fe::kData);
            if (!od || !cd) continue;
            const int alpha = field<int>(od, fe::kDataAlpha);
            if (field<int>(cd, fe::kDataAlpha) != alpha) {
                field<int>(cd, fe::kDataAlpha) = alpha;
                field<unsigned>(p.copy, fe::kFlags) |= fe::kDirtyColor | fe::kDirty;
            }
        }
    }

    // Whether `obj` is one of the clone's objects (a Defaults message from it is a click on ours).
    bool contains(void *obj) const {
        for (int i = 0; i < n_; ++i)
            if (pair_[i].copy == obj) return true;
        return false;
    }

    bool on(void *screen) const { return clone_ && screen_ == screen; }
    void *root() const { return clone_; }
    int objects() const { return n_; }

    // The screen is gone (the package and the clone with it).
    void clear() {
        screen_ = clone_ = nullptr;
        n_ = 0;
    }

  private:
    struct Pair {
        void *orig;
        void *copy;
        bool mirrored;  // runs the original's appear/fade scripts (everything except the hit-box)
    };

    static bool fail(const char **why, const char *text) {
        if (why) *why = text;
        return false;
    }

    // Each cloned object gets its own name hash and GUID: the mouse code finds a hit-box's screen offset by name (OffsetCalculatron), and responses and
    // script events address objects by GUID, so with the original's names the clone's hit area landed on the Defaults hint.
    void prepare(void *orig, void *copy, int depth, bool mirrored) {
        if (field<unsigned>(copy, fe::kFlags) & fe::kMouseObject) mirrored = false;  // the hit-box keeps its own hover look
        if (n_ < kMaxObjects) {
            const int i = n_++;
            pair_[i] = {orig, copy, mirrored};
            field<unsigned>(copy, fe::kNameHash) = i == 0 ? look_.name_base : look_.name_base + 0x100 + i;
            field<unsigned>(copy, fe::kGuid) = look_.guid_base + i;
        }
        copy_object_state(orig, copy);

        const int type = field<int>(copy, fe::kType);
        if (type == fe::kString) {  // the label
            game::FEPrintf(copy, "%s", look_.label);
        } else if (type == fe::kImage) {  // the key glyph (the hit-box's backing image keeps its texture)
            if (is_key_glyph(field<unsigned>(copy, fe::kHandle))) game::FEngSetTextureHash(copy, look_.key_texture);
        } else if (type == fe::kGroup && depth < 4) {
            void *o = field<void *>(orig, fe::kChildHead);
            void *c = field<void *>(copy, fe::kChildHead);
            for (unsigned i = 0, count = field<unsigned>(copy, fe::kChildCount); o && c && i < count && i < 16; ++i) {
                prepare(o, c, depth + 1, mirrored);
                o = field<void *>(o, fe::kNext);
                c = field<void *>(c, fe::kNext);
            }
        }
    }

    // A click runs the hit-box's message responses. On the clone, the response that posts BUTTON5 (Defaults) posts our own message straight to the screen
    // instead, and responses aimed at one of the original's objects (by GUID) are aimed at the clone's counterpart.
    void retarget_responses() {
        for (int i = 0; i < n_; ++i) {
            void *ro = field<void *>(pair_[i].orig, fe::kResponseHead);
            void *rc = field<void *>(pair_[i].copy, fe::kResponseHead);
            for (unsigned k = 0, n = field<unsigned>(pair_[i].copy, fe::kResponseCount); rc && k < n && k < 32; ++k) {
                unsigned *list = field<unsigned *>(rc, 0x14);
                const unsigned *olist = ro ? field<unsigned *>(ro, 0x14) : nullptr;
                for (unsigned j = 0, cnt = field<unsigned>(rc, 0x10); list && list != olist && j < cnt && j < 16; ++j) {
                    unsigned *r = list + j * 3;  // {ID, Param, Target}
                    if (r[1] == kButton5Msg && (r[0] == 1 || r[0] == 2)) {
                        r[0] = 2;  // MR_PostMessageToGame: straight to the screen
                        r[1] = look_.click_msg;
                    }
                    for (int m = 0; m < n_ && r[2]; ++m) {
                        if (r[2] == field<unsigned>(pair_[m].orig, fe::kGuid)) {
                            r[2] = field<unsigned>(pair_[m].copy, fe::kGuid);
                            break;
                        }
                    }
                }
                rc = field<void *>(rc, fe::kNext);
                ro = ro ? field<void *>(ro, fe::kNext) : nullptr;
            }
        }
    }

    // The label's hover look comes from script events, not responses: the hit-box's mouse-over script posts 0x696851AD (label: HIGHLIGHT) and its
    // mouse-exit script posts 0x1C8E4190 (label: UNHIGHLIGHT), both addressed to the whole package. The package hands them to the objects in its message
    // target lists, which name the Defaults label only. So on the clone, such an event is addressed straight to the one clone object that responds to it,
    // and events aimed at one of the original's objects are aimed at the clone's counterpart. (Each cloned script has its own events.)
    void retarget_script_events() {
        for (int i = 0; i < n_; ++i) {
            void *so = field<void *>(pair_[i].orig, fe::kScriptHead);
            void *sc = field<void *>(pair_[i].copy, fe::kScriptHead);
            for (unsigned k = 0, n = field<unsigned>(pair_[i].copy, fe::kScriptCount); sc && k < n && k < 32; ++k) {
                unsigned *ev = field<unsigned *>(sc, 0x28);
                const unsigned *oev = so ? field<unsigned *>(so, 0x28) : nullptr;
                for (unsigned j = 0, cnt = field<unsigned>(sc, 0x24); ev && ev != oev && j < cnt && j < 32; ++j) {
                    const unsigned msg = ev[j * 3];
                    unsigned &target = ev[j * 3 + 1];
                    if (target == 0 || target == 0xFFFFFFFC) {  // to everyone / to the package
                        int responder = -1, responders = 0;
                        for (int m = 0; m < n_; ++m)
                            if (responds_to(pair_[m].orig, msg)) {
                                responder = m;
                                ++responders;
                            }
                        if (responders == 1 && msg != kEventSetButton) target = field<unsigned>(pair_[responder].copy, fe::kGuid);
                    } else {
                        for (int m = 0; m < n_; ++m)
                            if (target == field<unsigned>(pair_[m].orig, fe::kGuid)) {
                                target = field<unsigned>(pair_[m].copy, fe::kGuid);
                                break;
                            }
                    }
                }
                sc = field<void *>(sc, fe::kNext);
                so = so ? field<void *>(so, fe::kNext) : nullptr;
            }
        }
    }

    static constexpr int kMaxObjects = 32;
    Look look_{};
    void *screen_ = nullptr;
    void *clone_ = nullptr;
    int n_ = 0;
    Pair pair_[kMaxObjects];
};

}  // namespace nfsmw::fe_hint
