// Runtime helpers for the (on-disk encrypted) main module: sections, string/xref search, function lookup.
// Everything here works on the decrypted image in memory after normal startup.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace Mem {

struct Section { uint8_t* start; size_t size; char name[9]; };

struct Module {
    uint8_t* base = nullptr;
    size_t size = 0;
    std::vector<Section> sections;
};

inline Module& Main() {
    static Module m;
    if (!m.base) {
        m.base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m.base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(m.base + dos->e_lfanew);
        m.size = nt->OptionalHeader.SizeOfImage;
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            Section s{};
            s.start = m.base + sec[i].VirtualAddress;
            s.size = sec[i].Misc.VirtualSize;
            memcpy(s.name, sec[i].Name, 8);
            s.name[8] = 0;
            m.sections.push_back(s);
        }
    }
    return m;
}

// First section with this name (the main module has two ".text"; the first is the game code).
inline const Section* Find(const char* name) {
    for (auto& s : Main().sections)
        if (strcmp(s.name, name) == 0) return &s;
    return nullptr;
}

inline uintptr_t Rva(const void* p) { return reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(Main().base); }

inline bool InModule(const void* p) {
    auto a = reinterpret_cast<const uint8_t*>(p);
    return a >= Main().base && a < Main().base + Main().size;
}

// Heuristic: the game code is decrypted once most function entries (from .pdata) start with a
// typical x64 prologue byte pattern.
inline bool CodeLooksDecrypted() {
    auto pdata = Find(".pdata");
    auto text = Find(".text");
    if (!pdata || !text) return false;
    auto rf = reinterpret_cast<RUNTIME_FUNCTION*>(pdata->start);
    size_t n = pdata->size / sizeof(RUNTIME_FUNCTION);
    int checked = 0, good = 0;
    for (size_t i = 0; i < n && checked < 400; i += n / 400 + 1) {
        uint8_t* f = Main().base + rf[i].BeginAddress;
        if (f < text->start || f >= text->start + text->size) continue;
        ++checked;
        uint8_t b0 = f[0], b1 = f[1];
        if ((b0 == 0x48 || b0 == 0x4C) && (b1 == 0x89 || b1 == 0x8B || b1 == 0x83 || b1 == 0x81 || b1 == 0x8D)) ++good;
        else if (b0 == 0x40 && (b1 >= 0x53 && b1 <= 0x57)) ++good;          // push rbx..rdi (REX)
        else if (b0 >= 0x53 && b0 <= 0x57) ++good;                           // push rbx..rdi
        else if (b0 == 0x41 && (b1 >= 0x54 && b1 <= 0x57)) ++good;          // push r12..r15
        else if (b0 == 0xE9 || b0 == 0xC3 || b0 == 0x33 || b0 == 0x8B || b0 == 0x0F || b0 == 0xB8 || b0 == 0x4D) ++good;
    }
    return checked > 50 && good * 100 / checked >= 60;
}

// Find a UTF-16 string literal in read-only data. Returns address or nullptr.
inline const uint8_t* FindWide(const wchar_t* s) {
    auto rdata = Find(".rdata");
    if (!rdata) return nullptr;
    size_t len = (wcslen(s) + 1) * sizeof(wchar_t);  // include terminator: exact literal
    const uint8_t* p = rdata->start;
    const uint8_t* end = rdata->start + rdata->size - len;
    for (; p < end; p += 2)
        if (memcmp(p, s, len) == 0) return p;
    return nullptr;
}

// Find instructions in .text that load the address `target` RIP-relatively (lea/mov r64, [rip+disp32]).
inline std::vector<uint8_t*> FindXrefs(const void* target, size_t maxHits = 8) {
    std::vector<uint8_t*> hits;
    auto text = Find(".text");
    if (!text) return hits;
    uint8_t* p = text->start;
    uint8_t* end = text->start + text->size - 7;
    auto t = reinterpret_cast<intptr_t>(target);
    for (; p < end && hits.size() < maxHits; ++p) {
        // REX.W/REX.WR + 8D (lea) + ModRM with mod=00 rm=101 (RIP-relative)
        if ((p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8D && (p[2] & 0xC7) == 0x05) {
            int32_t disp;
            memcpy(&disp, p + 3, 4);
            if (reinterpret_cast<intptr_t>(p + 7) + disp == t) hits.push_back(p);
        }
    }
    return hits;
}

// Function start containing `addr`, using the image's unwind table.
inline uint8_t* FunctionStart(void* addr) {
    DWORD64 imageBase = 0;
    auto rf = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(addr), &imageBase, nullptr);
    if (!rf) return nullptr;
    // Chained unwind info: walk back to the primary entry.
    for (int guard = 0; guard < 32; ++guard) {
        auto ui = reinterpret_cast<uint8_t*>(imageBase + rf->UnwindData);
        uint8_t flags = ui[0] >> 3;
        if (!(flags & UNW_FLAG_CHAININFO)) break;
        uint8_t count = ui[2];
        auto chained = reinterpret_cast<RUNTIME_FUNCTION*>(ui + 4 + ((count + 1) & ~1) * 2);
        rf = chained;
    }
    return reinterpret_cast<uint8_t*>(imageBase + rf->BeginAddress);
}

// Slots in read-only data holding a pointer to `fn` (vtable entries).
inline std::vector<void**> FindPointerSlots(const void* fn, size_t maxHits = 8) {
    std::vector<void**> hits;
    auto rdata = Find(".rdata");
    if (!rdata) return hits;
    auto p = reinterpret_cast<void**>(rdata->start);
    auto end = reinterpret_cast<void**>(rdata->start + rdata->size);
    for (; p < end && hits.size() < maxHits; ++p)
        if (*p == fn) hits.push_back(p);
    return hits;
}

inline bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    auto end = reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    return reinterpret_cast<const uint8_t*>(p) + n <= end;
}

inline void WritePtr(void** slot, void* value) {
    DWORD old;
    VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
}

}  // namespace Mem
