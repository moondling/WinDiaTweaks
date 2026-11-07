// history.cpp - the Hide history suggestions tweak: pages you've visited and searches you've made before
// stay out of the suggestions in the address field and the command bar (Ctrl+L, Ctrl+T, New Tab).
// Searching, Chat, search suggestions and open tabs still show. History itself is untouched.
//
// Like DiaTweaks on macOS, this filters Chromium's autocomplete results before Dia sees them, rather than
// hiding rows on screen: the list, the arrow keys, Enter and the text Dia completes inline all follow.
//
// Dia's Chromium (ArcCore.dll) hands Dia its suggestions as WinRT objects: an AutocompleteResult whose
// matches come in ArcCoreWinRT.AutocompleteMatchArray objects (get_Size / ElementAt), each match an
// ArcCoreWinRT.AutocompleteMatch with its provider name, match type and whether an open tab has it.
// Dia ships no metadata for these types; their method order comes from the vtable structs Dia's Swift
// code imports (Dia.dll's Swift field metadata). The array's get_Size and ElementAt are replaced in
// ArcCore's vtable for that class, so every array (combined and per provider) skips history matches.
// Providers left with no matches are dropped from the per-provider map too, as macOS does.
//
// The vtables are found when the tweak is first turned on, not at fixed addresses, so a Dia update
// doesn't break them: each class's GetRuntimeClassName (slot 4) loads its name ("ArcCoreWinRT....")
// with a lea; the function holding that lea is slot 4 of the vtable. If anything doesn't look as
// expected, nothing is patched and the tweak does nothing.
//
// Which matches go (the same rule as on macOS): provider History* (HistoryURL, HistoryQuick,
// HistoryFuzzy, ...) or Shortcuts, match type history-* or search-history; never a match an open tab has.

#include <windows.h>
#include <winstring.h>
#include <roapi.h>

#include <atomic>
#include <string_view>
#include <thread>
#include <vector>

#include "engine.h"

#pragma comment(lib, "runtimeobject.lib")

#define Log Engine_Log

namespace {

// IAutocompleteMatchArray (after IInspectable's 6 slots)
constexpr int kGetSize = 6, kElementAt = 7;
// IAutocompleteMatch
constexpr int kGetMatchType = 10, kGetProviderType = 11, kGetHasTabMatch = 15;
constexpr int kVtableSlots = 8;  // checked to point into ArcCore's code before anything is patched

using GetSizeFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT32*);
using ElementAtFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT32, void**);
using GetStringFn = HRESULT(STDMETHODCALLTYPE*)(void*, HSTRING*);
using GetBoolFn = HRESULT(STDMETHODCALLTYPE*)(void*, boolean*);
using GetObjFn = HRESULT(STDMETHODCALLTYPE*)(void*, void**);
using StringAtFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT32, HSTRING*);
using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(void*);
// IStringToAutocompleteMatchesMap
constexpr int kGetKeys = 6, kGetValues = 7;

std::atomic<bool> g_on{false};
std::atomic<int> g_state{0};  // 0 not tried, 1 searching, 2 patched, 3 not found
GetSizeFn g_size = nullptr;
ElementAtFn g_at = nullptr;
void** g_matchVtable = nullptr;

struct Section {
    BYTE* start = nullptr;
    size_t size = 0;
    bool Has(const void* p) const { return (const BYTE*)p >= start && (const BYTE*)p < start + size; }
};

struct Image {
    BYTE* base = nullptr;
    Section text, rdata;
};

bool LoadImage(Image& img) {
    auto base = (BYTE*)GetModuleHandleW(L"ArcCore.dll");
    if (!base) return false;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    img.base = base;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        char name[9] = {};
        memcpy(name, sec->Name, 8);
        Section s{base + sec->VirtualAddress, sec->Misc.VirtualSize};
        if (!strcmp(name, ".text")) img.text = s;
        else if (!strcmp(name, ".rdata")) img.rdata = s;
    }
    return img.text.start && img.rdata.start;
}

// The class's name as a whole UTF-16 string in .rdata ("\0" before it, so not the tail of a longer name).
const BYTE* FindName(const Image& img, std::wstring_view name) {
    size_t bytes = (name.size() + 1) * sizeof(wchar_t);
    const BYTE* p = img.rdata.start + 2;
    const BYTE* end = img.rdata.start + img.rdata.size - bytes;
    for (; p < end; p += 2) {
        if (*(const wchar_t*)p != name[0]) continue;
        if (*(const wchar_t*)(p - 2) != 0) continue;
        if (memcmp(p, name.data(), name.size() * sizeof(wchar_t)) == 0 && *(const wchar_t*)(p + bytes - 2) == 0)
            return p;
    }
    return nullptr;
}

// Code in .text that loads `target` with lea reg, [rip+disp32] (48/4C 8D, mod 00 rm 101).
std::vector<const BYTE*> FindLeas(const Image& img, const BYTE* target) {
    std::vector<const BYTE*> out;
    const BYTE* t = img.text.start;
    size_t n = img.text.size;
    for (size_t i = 1; i + 6 < n; i++) {
        if (t[i] != 0x8D || (t[i - 1] & 0xFB) != 0x48 || (t[i + 1] & 0xC7) != 0x05) continue;
        int32_t disp;
        memcpy(&disp, t + i + 2, 4);
        if (t + i + 6 + disp == target) out.push_back(t + i - 1);
    }
    return out;
}

bool LooksLikeVtable(const Image& img, void** vt) {
    if (!img.rdata.Has(vt) || !img.rdata.Has(vt + kVtableSlots - 1)) return false;
    for (int i = 0; i < kVtableSlots; i++)
        if (!img.text.Has(vt[i])) return false;
    return true;
}

// The vtable whose GetRuntimeClassName returns `name`: slot 4 is the function that loads the name, which
// starts a few bytes before that lea.
void** FindVtable(const Image& img, std::wstring_view name) {
    const BYTE* str = FindName(img, name);
    if (!str) return nullptr;
    auto leas = FindLeas(img, str);
    if (leas.empty()) return nullptr;
    void** found = nullptr;
    int count = 0;
    auto q = (void**)img.rdata.start;
    size_t nq = img.rdata.size / sizeof(void*);
    for (size_t i = 4; i < nq; i++) {
        auto f = (const BYTE*)q[i];
        if (!img.text.Has(f)) continue;
        for (auto lea : leas) {
            if (f > lea || lea - f > 0x40) continue;
            void** vt = q + i - 4;
            if (!LooksLikeVtable(img, vt)) continue;
            if (vt != found) found = vt, count++;
        }
    }
    return count == 1 ? found : nullptr;  // more than one: not sure which, leave it
}

std::wstring_view View(HSTRING s) {
    UINT32 len = 0;
    const wchar_t* p = WindowsGetStringRawBuffer(s, &len);
    return {p ? p : L"", len};
}

bool IsHistory(void* match) {
    if (!match || *(void***)match != g_matchVtable) return false;
    void** vt = *(void***)match;
    boolean tab = false;
    if (SUCCEEDED(((GetBoolFn)vt[kGetHasTabMatch])(match, &tab)) && tab) return false;
    HSTRING provider = nullptr, type = nullptr;
    bool history = false;
    if (SUCCEEDED(((GetStringFn)vt[kGetProviderType])(match, &provider)) &&
        SUCCEEDED(((GetStringFn)vt[kGetMatchType])(match, &type))) {
        auto p = View(provider), t = View(type);
        history = p.starts_with(L"History") || p == L"Shortcuts" || t.starts_with(L"history-") ||
                  t == L"search-history";
    }
    if (provider) WindowsDeleteString(provider);
    if (type) WindowsDeleteString(type);
    return history;
}

// An array (get_Size / ElementAt) seen without the elements `drop` says no to.
using DropFn = bool (*)(void*);

HRESULT FilteredSize(void* self, GetSizeFn size, ElementAtFn at, DropFn drop, UINT32* out) {
    HRESULT hr = size(self, out);
    if (FAILED(hr) || !out || !g_on.load(std::memory_order_relaxed)) return hr;
    UINT32 n = *out, kept = 0;
    for (UINT32 i = 0; i < n; i++) {
        void* e = nullptr;
        if (FAILED(at(self, i, &e))) {
            kept++;
            continue;
        }
        if (!drop(e)) kept++;
        if (e) ((IUnknown*)e)->Release();
    }
    *out = kept;
    return hr;
}

HRESULT FilteredAt(void* self, GetSizeFn size, ElementAtFn at, DropFn drop, UINT32 index, void** out) {
    if (!g_on.load(std::memory_order_relaxed)) return at(self, index, out);
    UINT32 n = 0;
    if (FAILED(size(self, &n))) return at(self, index, out);
    UINT32 k = 0;
    for (UINT32 i = 0; i < n; i++) {
        void* e = nullptr;
        if (FAILED(at(self, i, &e))) {
            if (k++ == index) return at(self, i, out);
            continue;
        }
        if (!drop(e)) {
            if (k++ == index) {
                *out = e;
                return S_OK;
            }
        }
        if (e) ((IUnknown*)e)->Release();
    }
    if (out) *out = nullptr;
    return E_BOUNDS;
}

// AutocompleteMatchArray: no history matches.
HRESULT STDMETHODCALLTYPE HookGetSize(void* self, UINT32* out) {
    return FilteredSize(self, g_size, g_at, IsHistory, out);
}
HRESULT STDMETHODCALLTYPE HookElementAt(void* self, UINT32 index, void** out) {
    return FilteredAt(self, g_size, g_at, IsHistory, index, out);
}

// ---- providers left with no matches (as macOS does when it rebuilds the result)
// The result's per-provider matches are a StringToAutocompleteMatchesMap: get_Keys (provider names, a
// StringArray) and get_Values (an AutocompleteMatchesArray of AutocompleteMatchArray, same order).
// The values array skips lists that are empty once history is out (patched for its class); the keys a
// map hands out are made to skip the same ones (patched for that one StringArray: it's a class used
// for any list of strings, so not for the class).

GetSizeFn g_listsSize = nullptr;  // AutocompleteMatchesArray
ElementAtFn g_listsAt = nullptr;
void** g_listsVtable = nullptr;
void** g_arrayVtable = nullptr;    // AutocompleteMatchArray
void** g_stringsVtable = nullptr;  // StringArray
GetObjFn g_getKeys = nullptr;      // StringToAutocompleteMatchesMap
GetObjFn g_getValues = nullptr;

bool IsEmptyList(void* list) {
    if (!list || *(void***)list != g_arrayVtable) return false;
    UINT32 n = 1;
    return SUCCEEDED(HookGetSize(list, &n)) && n == 0;
}

HRESULT STDMETHODCALLTYPE HookListsSize(void* self, UINT32* out) {
    return FilteredSize(self, g_listsSize, g_listsAt, IsEmptyList, out);
}
HRESULT STDMETHODCALLTYPE HookListsAt(void* self, UINT32 index, void** out) {
    return FilteredAt(self, g_listsSize, g_listsAt, IsEmptyList, index, out);
}

// A keys StringArray that skips some names: its own copy of the vtable, with the indices kept after it.
// Freed when the array is.
struct KeysPatch {
    void* vt[16];  // first: the object's vtable pointer points here (past the interface too: Release
                   // reaches the class's destructor through it)
    GetSizeFn size;
    StringAtFn at;
    ReleaseFn release;
    std::vector<UINT32> kept;
};

KeysPatch* PatchOf(void* self) { return (KeysPatch*)*(void***)self; }

HRESULT STDMETHODCALLTYPE HookKeysSize(void* self, UINT32* out) {
    auto kp = PatchOf(self);
    if (!g_on.load(std::memory_order_relaxed)) return kp->size(self, out);
    if (!out) return E_POINTER;
    *out = (UINT32)kp->kept.size();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE HookKeysAt(void* self, UINT32 index, HSTRING* out) {
    auto kp = PatchOf(self);
    if (!g_on.load(std::memory_order_relaxed)) return kp->at(self, index, out);
    if (index >= kp->kept.size()) {
        if (out) *out = nullptr;
        return E_BOUNDS;
    }
    return kp->at(self, kp->kept[index], out);
}
ULONG STDMETHODCALLTYPE HookKeysRelease(void* self) {
    auto kp = PatchOf(self);
    ULONG left = kp->release(self);
    if (left == 0) delete kp;  // the array is gone (its destructor set its own vtable back)
    return left;
}

HRESULT STDMETHODCALLTYPE HookGetKeys(void* self, void** out) {
    HRESULT hr = g_getKeys(self, out);
    if (FAILED(hr) || !out || !*out || !g_on.load(std::memory_order_relaxed)) return hr;
    // Which providers still have matches (the values as they really are, in order).
    void* values = nullptr;
    if (FAILED(g_getValues(self, &values)) || !values) return hr;
    std::vector<UINT32> kept;
    UINT32 n = 0;
    bool known = *(void***)values == g_listsVtable && SUCCEEDED(g_listsSize(values, &n));
    for (UINT32 i = 0; known && i < n; i++) {
        void* list = nullptr;
        if (FAILED(g_listsAt(values, i, &list))) {
            kept.push_back(i);
            continue;
        }
        if (!IsEmptyList(list)) kept.push_back(i);
        if (list) ((IUnknown*)list)->Release();
    }
    ((IUnknown*)values)->Release();
    if (!known) return hr;
    void* keys = *out;
    void** vt = *(void***)keys;
    if (vt[2] == (void*)&HookKeysRelease) {  // the map handed out these keys before
        PatchOf(keys)->kept = std::move(kept);
        return hr;
    }
    UINT32 nk = 0;
    if (kept.size() == n || vt != g_stringsVtable || FAILED(((GetSizeFn)vt[kGetSize])(keys, &nk)) || nk != n)
        return hr;
    auto kp = new KeysPatch;
    memcpy(kp->vt, vt, sizeof(kp->vt));
    kp->size = (GetSizeFn)vt[kGetSize];
    kp->at = (StringAtFn)vt[kElementAt];
    kp->release = (ReleaseFn)vt[2];
    kp->kept = std::move(kept);
    kp->vt[2] = (void*)&HookKeysRelease;
    kp->vt[kGetSize] = (void*)&HookKeysSize;
    kp->vt[kElementAt] = (void*)&HookKeysAt;
    InterlockedExchangePointer((void**)keys, kp->vt);
    return hr;
}

bool PatchSlot(void** slot, void* fn) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *(void* volatile*)slot = fn;
    VirtualProtect(slot, sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return true;
}

void Install() {
    DWORD t0 = GetTickCount();
    Image img;
    if (!LoadImage(img)) {
        Log("hide history suggestions: ArcCore.dll not loaded here");
        g_state = 3;
        return;
    }
    void** arrayVt = FindVtable(img, L"ArcCoreWinRT.AutocompleteMatchArray");
    void** matchVt = FindVtable(img, L"ArcCoreWinRT.AutocompleteMatch");
    if (!arrayVt || !matchVt) {
        Log("hide history suggestions: Dia's suggestion types not found (array %p, match %p); not patched",
            arrayVt, matchVt);
        g_state = 3;
        return;
    }
    g_matchVtable = matchVt;
    g_arrayVtable = arrayVt;
    g_size = (GetSizeFn)arrayVt[kGetSize];
    g_at = (ElementAtFn)arrayVt[kElementAt];
    // ElementAt first: until get_Size is patched the two still agree with each other.
    if (!PatchSlot(arrayVt + kElementAt, (void*)&HookElementAt) || !PatchSlot(arrayVt + kGetSize, (void*)&HookGetSize)) {
        Log("hide history suggestions: couldn't patch");
        g_state = 3;
        return;
    }
    g_state = 2;
    // Providers left empty: optional, history is hidden either way.
    void** listsVt = FindVtable(img, L"ArcCoreWinRT.AutocompleteMatchesArray");
    void** mapVt = FindVtable(img, L"ArcCoreWinRT.StringToAutocompleteMatchesMap");
    void** stringsVt = FindVtable(img, L"ArcCoreWinRT.StringArray");
    bool groups = listsVt && mapVt && stringsVt;
    if (groups) {
        g_stringsVtable = stringsVt;
        g_listsVtable = listsVt;
        g_listsSize = (GetSizeFn)listsVt[kGetSize];
        g_listsAt = (ElementAtFn)listsVt[kElementAt];
        g_getKeys = (GetObjFn)mapVt[kGetKeys];
        g_getValues = (GetObjFn)mapVt[kGetValues];
        groups = PatchSlot(listsVt + kElementAt, (void*)&HookListsAt) && PatchSlot(listsVt + kGetSize, (void*)&HookListsSize) &&
                 PatchSlot(mapVt + kGetKeys, (void*)&HookGetKeys);
    }
    Log("hide history suggestions: ready (ArcCore+%llx, empty providers %s, %lu ms)",
        (unsigned long long)((BYTE*)arrayVt - img.base), groups ? "dropped" : "kept (types not found)",
        GetTickCount() - t0);
}

}  // namespace

// Called every tick on Dia's UI threads.
void History_Tick() {
    bool on = Engine_TweakOn(L"hideHistorySuggestions");
    if (on != g_on.exchange(on)) Log("hide history suggestions: %s", on ? "on" : "off");
    int expected = 0;
    if (on && g_state.compare_exchange_strong(expected, 1))
        std::thread([] {
            __try {
                Install();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("hide history suggestions: search failed");
                g_state = 3;
            }
        }).detach();
}
