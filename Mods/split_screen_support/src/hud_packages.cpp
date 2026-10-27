/*
 * The game keeps every FEng package of the loaded bun files in FEPackageManager's list (FEPackageData, 0x38 bytes, made by LoaderFEngPackage 0x585AA0
 * for each package chunk of a bun: chunk at +0x18). A package's name is in its data (the header chunk "PkHd": short name at +0x28 of the FEng chunk's
 * data, then the file name), and the HUD, the FEng engine and the package manager all find a package by that name. The copy is a new chunk (id
 * 0x30203, the uncompressed form, size, then the package's data) with the names changed, handed to LoaderFEngPackage like a package of a bun.
 * The packages of the bun are compressed (id 0x30210: the name hash, then a JDLZ block whose header has the uncompressed size at +8):
 * LZDecompress 0x650350 (cdecl, source, destination).
 */
#include "hud_packages.h"

#include <cstdlib>
#include <cstring>

#include "split_screen_support.h"

namespace split_screen_support::hud_packages {

namespace {

constexpr uintptr_t kGetManager = 0x572070, kFindPackageData = 0x585820, kLoaderFEngPackage = 0x585AA0, kLZDecompress = 0x650350;
constexpr unsigned kPackageChunk = 0x30203, kCompressedPackageChunk = 0x30210;
constexpr int kDataChunk = 0x18;  // FEPackageData: the bChunk

void *find_data(const char *name) {
    void *manager = reinterpret_cast<void *(*)()>(kGetManager)();
    return manager ? reinterpret_cast<void *(__fastcall *)(void *, void *, const char *)>(kFindPackageData)(manager, nullptr, name) : nullptr;
}

// Replaces every `from` with `to` (same length) in the first `size` bytes.
int replace_in(unsigned char *bytes, size_t size, const char *from, const char *to) {
    const size_t length = strlen(from);
    int count = 0;
    for (size_t i = 0; i + length <= size; ++i)
        if (memcmp(bytes + i, from, length) == 0) {
            memcpy(bytes + i, to, length);
            i += length - 1;
            ++count;
        }
    return count;
}

}  // namespace

bool ensure_alias(const char *source, const char *alias) {
    if (strlen(source) != strlen(alias)) {
        g_log.line("hud packages: %s and %s have different lengths", source, alias);
        return false;
    }
    if (find_data(alias)) return true;
    void *original = find_data(source);
    if (!original) {
        g_log.line("hud packages: %s is not loaded: no copy %s", source, alias);
        return false;
    }
    const unsigned char *chunk = *reinterpret_cast<unsigned char **>(static_cast<char *>(original) + kDataChunk);
    const unsigned id = *reinterpret_cast<const unsigned *>(chunk);
    const unsigned char *payload = chunk + 8;
    unsigned char *copy = nullptr;
    size_t size = 0;
    if (id == kCompressedPackageChunk) {
        const unsigned char *block = payload + 4;  // after the name hash
        size = *reinterpret_cast<const unsigned *>(block + 8);
        copy = static_cast<unsigned char *>(malloc(8 + size));
        if (!copy) return false;
        const unsigned got = reinterpret_cast<unsigned(__cdecl *)(const unsigned char *, unsigned char *)>(kLZDecompress)(block, copy + 8);
        if (got != size) {
            g_log.line("hud packages: %s decompressed to %u bytes, not %u: no copy", source, got, static_cast<unsigned>(size));
            free(copy);
            return false;
        }
    } else if (id == kPackageChunk) {
        size = *reinterpret_cast<const unsigned *>(chunk + 4);
        copy = static_cast<unsigned char *>(malloc(8 + size));
        if (!copy) return false;
        memcpy(copy + 8, payload, size);
    } else {
        g_log.line("hud packages: %s is a chunk of type %#x: no copy", source, id);
        return false;
    }
    *reinterpret_cast<unsigned *>(copy) = kPackageChunk;
    *reinterpret_cast<unsigned *>(copy + 4) = static_cast<unsigned>(size);

    // The names: the base name ("HUD_Player1") and with the extension, as the header has them.
    char from_base[64], to_base[64];
    strncpy(from_base, source, sizeof(from_base) - 1);
    strncpy(to_base, alias, sizeof(to_base) - 1);
    from_base[sizeof(from_base) - 1] = to_base[sizeof(to_base) - 1] = 0;
    if (char *dot = strrchr(from_base, '.')) *dot = 0;
    if (char *dot = strrchr(to_base, '.')) *dot = 0;
    const size_t header = size < 0x200 ? size : 0x200;
    const int renamed = replace_in(copy + 8, header, from_base, to_base);
    g_log.line("hud packages: %s copied from %s (%u bytes, the name appears %d times in its header: \"%.40s\")", alias, source, static_cast<unsigned>(size), renamed,
               reinterpret_cast<const char *>(copy + 8 + 0x28));
    if (!renamed) {
        free(copy);
        return false;
    }
    reinterpret_cast<int(__cdecl *)(void *)>(kLoaderFEngPackage)(copy);
    if (!find_data(alias)) {
        g_log.line("hud packages: %s was not registered", alias);
        return false;
    }
    return true;
}

}  // namespace split_screen_support::hud_packages
