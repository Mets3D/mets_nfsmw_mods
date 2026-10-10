// Changing the game's code in memory: bytes replaced only if they are still the ones expected, and small pieces of
// machine code (stubs) that a patched place can jump to. Header only.
#pragma once

#include "nfsmw_util.h"

namespace nfsmw {

// Replaces `length` bytes at `addr` with `replacement`, but only if they are still `expected` (another mod may have
// changed them). Returns whether it did.
inline bool patch_bytes(uintptr_t addr, const unsigned char *expected, const unsigned char *replacement, size_t length) {
    void *at = reinterpret_cast<void *>(addr);
    if (memcmp(at, expected, length) != 0) return false;
    DWORD old = 0;
    if (!VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, replacement, length);
    VirtualProtect(at, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, length);
    return true;
}

// Machine code in memory of its own, written byte by byte. Short jumps are emitted with a hole (jump_short) that
// resolve_short() fills in with the distance to the current position.
class Stub {
  public:
    explicit Stub(size_t capacity = 128)
        : capacity_(capacity),
          code_(static_cast<unsigned char *>(VirtualAlloc(nullptr, capacity, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))) {}
    bool ok() const { return code_ && !overflow_; }
    uintptr_t address() const { return reinterpret_cast<uintptr_t>(code_); }

    void emit(std::initializer_list<unsigned char> bytes) {
        for (const unsigned char b : bytes) put(b);
    }
    // `opcode` (E8 call, E9 jmp) followed by the distance to `target`.
    void emit_rel32(unsigned char opcode, uintptr_t target) {
        put(opcode);
        const intptr_t rel = static_cast<intptr_t>(target) - static_cast<intptr_t>(address() + used_ + 4);
        for (int i = 0; i < 4; ++i) put(static_cast<unsigned char>(static_cast<uintptr_t>(rel) >> (8 * i)));
    }
    // A short jump (74 je, 75 jne, EB jmp...) whose target comes later; returns the hole to pass to resolve_short().
    size_t jump_short(unsigned char opcode) {
        emit({opcode, 0});
        return used_ - 1;
    }
    // Makes the short jump with this hole land on the next byte emitted.
    void resolve_short(size_t hole) {
        if (code_ && hole < used_) code_[hole] = static_cast<unsigned char>(used_ - hole - 1);
    }
    void done() {
        if (code_) FlushInstructionCache(GetCurrentProcess(), code_, used_);
    }

  private:
    void put(unsigned char b) {
        if (code_ && used_ < capacity_) code_[used_++] = b;
        else overflow_ = true;
    }
    size_t capacity_;
    unsigned char *code_;
    size_t used_ = 0;
    bool overflow_ = false;
};

// Makes the `length` (at least 5) bytes at `site`, which must be `expected`, a jump to the stub, the rest no-ops. The stub
// has to do what those bytes did and then jump to the code after them.
inline bool jump_to_stub(uintptr_t site, const unsigned char *expected, size_t length, const Stub &stub) {
    unsigned char patch[16];
    if (length < 5 || length > sizeof(patch) || !stub.ok()) return false;
    memset(patch, 0x90, length);
    patch[0] = 0xE9;
    const intptr_t rel = static_cast<intptr_t>(stub.address()) - static_cast<intptr_t>(site + 5);
    memcpy(patch + 1, &rel, 4);
    return patch_bytes(site, expected, patch, length);
}

}  // namespace nfsmw
