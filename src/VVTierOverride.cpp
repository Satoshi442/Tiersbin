// VVTierOverride - forces a GPU's device tier by patching tiers.bin as the
// game reads it from the APK assets. No files on disk are modified.
//
// How it works:
//   The game reads tiers.bin through the NDK asset API. We hook four libandroid
//   functions. When the game opens an asset named "*tiers.bin" we read it once,
//   patch it with vv::patchTiers(), and remember the patched copy. Reads and
//   getBuffer() calls on that asset then return the patched bytes. The patched
//   copy is the same size as the original, so offsets/length stay consistent.
//   Every other asset goes straight to the original functions.

#include <android/asset_manager.h>
#include <dlfcn.h>
#include <unistd.h>

#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

#include "TierPatch.hpp"

// ---- Edit these ------------------------------------------------------------
static constexpr std::string_view kAssetSuffix = "tiers.bin";
static constexpr std::string_view kGpuName = "Mali-G52 MC2";  // exact key in tiers.bin
static constexpr char kTier = '5';                            // single digit 1-5
// ----------------------------------------------------------------------------

namespace {

using OpenFn = AAsset *(*)(AAssetManager *, const char *, int);
using ReadFn = int (*)(AAsset *, void *, size_t);
using BufFn = const void *(*)(AAsset *);
using CloseFn = void (*)(AAsset *);

OpenFn gOpen;
ReadFn gRead;
BufFn gBuf;
CloseFn gClose;

std::mutex gMutex;
std::unordered_map<AAsset *, std::string> gPatched;  // asset -> patched bytes
ll::mod::NativeMod *gSelf;

const std::string *patchedFor(AAsset *a) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPatched.find(a);
    return it == gPatched.end() ? nullptr : &it->second;
}

AAsset *openHook(AAssetManager *mgr, const char *name, int mode) {
    AAsset *a = gOpen(mgr, name, mode);
    if (!a || !name) return a;

    std::string_view n(name);
    if (n.size() < kAssetSuffix.size() ||
        n.substr(n.size() - kAssetSuffix.size()) != kAssetSuffix)
        return a;

    // Read the whole original once (gRead = unhooked original), then rewind.
    off64_t len = AAsset_getLength64(a);
    std::string raw(static_cast<size_t>(len), '\0');
    int got = gRead(a, raw.data(), raw.size());
    AAsset_seek64(a, 0, SEEK_SET);
    if (got != len) {
        gSelf->getLogger().warn("{}: short read ({} of {})", name, got, (long)len);
        return a;
    }

    std::string patched = vv::patchTiers(raw, kGpuName, kTier);
    if (patched.empty()) {
        gSelf->getLogger().warn("{}: could not patch '{}' (left unchanged)", name,
                                std::string(kGpuName));
        return a;
    }

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gPatched[a] = std::move(patched);
    }
    gSelf->getLogger().info("{}: '{}' -> tier {}", name, std::string(kGpuName), kTier);
    return a;
}

int readHook(AAsset *a, void *buf, size_t count) {
    const std::string *p = patchedFor(a);
    if (!p) return gRead(a, buf, count);

    off64_t before = AAsset_getLength64(a) - AAsset_getRemainingLength64(a);
    int n = gRead(a, buf, count);  // advances the real position
    if (n > 0 && static_cast<size_t>(before) + n <= p->size())
        std::memcpy(buf, p->data() + before, static_cast<size_t>(n));
    return n;
}

const void *getBufferHook(AAsset *a) {
    const std::string *p = patchedFor(a);
    return p ? static_cast<const void *>(p->data()) : gBuf(a);
}

void closeHook(AAsset *a) {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gPatched.erase(a);
    }
    gClose(a);
}

struct HookSpec {
    const char *symbol;
    void *detour;
    void **original;
    void *target;
};

HookSpec gHooks[] = {
    {"AAssetManager_open", reinterpret_cast<void *>(&openHook),
     reinterpret_cast<void **>(&gOpen), nullptr},
    {"AAsset_read", reinterpret_cast<void *>(&readHook),
     reinterpret_cast<void **>(&gRead), nullptr},
    {"AAsset_getBuffer", reinterpret_cast<void *>(&getBufferHook),
     reinterpret_cast<void **>(&gBuf), nullptr},
    {"AAsset_close", reinterpret_cast<void *>(&closeHook),
     reinterpret_cast<void **>(&gClose), nullptr},
};

}  // namespace

class VVTierOverride {
public:
    static VVTierOverride &instance() {
        static VVTierOverride inst;
        return inst;
    }

    VVTierOverride() : mSelf(*ll::mod::NativeMod::current()) { gSelf = &mSelf; }

    bool load() { return true; }

    bool enable() {
        // All four must install, or none: a partial set would hand out
        // patched buffers that the other hooks don't know about.
        size_t installed = 0;
        for (auto &h : gHooks) {
            h.target = dlsym(RTLD_DEFAULT, h.symbol);
            if (!h.target ||
                pl::memory::hook(h.target, h.detour, h.original) != 0) {
                mSelf.getLogger().error("Failed to hook {}", h.symbol);
                break;
            }
            ++installed;
        }
        if (installed != std::size(gHooks)) {
            for (size_t i = 0; i < installed; ++i)
                pl::memory::unhook(gHooks[i].target, gHooks[i].detour);
            return false;
        }
        mSelf.getLogger().info("Hooks installed, waiting for {}", std::string(kAssetSuffix));
        return true;
    }

    bool disable() {
        for (auto &h : gHooks)
            if (h.target) pl::memory::unhook(h.target, h.detour);
        std::lock_guard<std::mutex> lock(gMutex);
        gPatched.clear();
        return true;
    }

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

private:
    ll::mod::NativeMod &mSelf;
};

PL_REGISTER_MOD(VVTierOverride, VVTierOverride::instance())
