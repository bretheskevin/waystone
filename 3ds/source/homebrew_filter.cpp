#include "homebrew_filter.h"
#include "net.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

static const char* const FETCH_URL  = "https://db.universal-team.net/data/full.json";
static const char* const CACHE_PATH = "sdmc:/waystone/homebrew_ids";
static const char* const CACHE_DIR  = "sdmc:/waystone";

static bool s_loaded = false;
static std::vector<uint64_t> s_ids; // sorted, for binary_search

// -------------------------------------------------------------------
// Lightweight manual scanner for the Universal-DB JSON body.
//
// Rationale for NOT using jsmn: the 900 KB document would require a
// large heap token array (the stack-overflow lesson from the 3DS memory),
// and even on the heap it would bloat peak usage unnecessarily. Instead
// we do a single linear pass looking only for "unique_ids" keys, then
// scan the bracketed integer list that follows. Bounded by the buffer
// end; no exceptions, no RTTI, no regex.
//
// title_id derivation (from task spec):
//   title_id = 0x0004000000000000 | ((uint64_t)unique_id << 8)
// -------------------------------------------------------------------
static const char* find_substr(const char* p, const char* end,
                                const char* needle, size_t nlen) {
    for (; p + nlen <= end; p++) {
        if (memcmp(p, needle, nlen) == 0) return p;
    }
    return nullptr;
}

static void parse_unique_ids_from_body(const char* buf, size_t len,
                                        std::vector<uint64_t>& ids) {
    static const char NEEDLE[] = "\"unique_ids\"";
    static const size_t NLEN = sizeof(NEEDLE) - 1;

    const char* p   = buf;
    const char* end = buf + len;

    while (p < end) {
        const char* hit = find_substr(p, end, NEEDLE, NLEN);
        if (!hit) break;
        p = hit + NLEN;

        // Skip whitespace + ':'
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                            *p == '\n' || *p == ':'))
            p++;
        if (p >= end || *p != '[') continue;
        p++; // skip '['

        // Read comma-separated integers until ']'
        while (p < end && *p != ']') {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                                *p == '\n' || *p == ','))
                p++;
            if (p >= end || *p == ']') break;
            if (*p >= '0' && *p <= '9') {
                char* ep;
                unsigned long uid = strtoul(p, &ep, 10);
                if (ep > p) {
                    if (uid <= 0xFFFFF) { // 20-bit unique_id range
                        uint64_t tid = 0x0004000000000000ULL | ((uint64_t)uid << 8);
                        ids.push_back(tid);
                    }
                    p = ep;
                } else {
                    p++;
                }
            } else {
                p++;
            }
        }
    }
}

// -------------------------------------------------------------------
// SD cache: one hex title_id per line ("0004000000BAF600\n" etc.).
// Tiny: only derived IDs, not the 900 KB JSON blob.
// -------------------------------------------------------------------
static void save_cache(const std::vector<uint64_t>& ids) {
    mkdir(CACHE_DIR, 0755); // ensure parent dir exists (non-fatal if already present)
    FILE* f = fopen(CACHE_PATH, "w");
    if (!f) {
        printf("[net] homebrew cache write failed path=%s\n", CACHE_PATH);
        return;
    }
    for (size_t i = 0; i < ids.size(); i++) {
        fprintf(f, "%016llX\n", (unsigned long long)ids[i]);
    }
    fclose(f);
    printf("[net] homebrew cache saved %zu ids -> %s\n", ids.size(), CACHE_PATH);
}

static bool load_cache(std::vector<uint64_t>& ids) {
    FILE* f = fopen(CACHE_PATH, "r");
    if (!f) return false;
    char line[32];
    while (fgets(line, sizeof(line), f)) {
        char* ep;
        unsigned long long v = strtoull(line, &ep, 16);
        if (ep > line) ids.push_back(static_cast<uint64_t>(v));
    }
    fclose(f);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    printf("[titles] homebrew cache loaded %zu ids from %s\n", ids.size(), CACHE_PATH);
    return !ids.empty();
}

// -------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------
void homebrew::ensure_loaded() {
    if (s_loaded) return;
    s_loaded = true;

    printf("[net] fetching homebrew list from %s\n", FETCH_URL);
    std::string body;
    int status = http_get(FETCH_URL, &body);

    if (status == 200 && !body.empty()) {
        std::vector<uint64_t> ids;
        parse_unique_ids_from_body(body.c_str(), body.size(), ids);
        // Free the 900 KB body as soon as parsing is done.
        std::string().swap(body);

        if (!ids.empty()) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            s_ids = ids;
            printf("[titles] %zu homebrew ids loaded (network)\n", s_ids.size());
            save_cache(s_ids);
            return;
        }
        // HTTP 200 but zero ids parsed — implausible; fall through to cache.
        // Do NOT hide all titles because the parse may have failed silently.
        printf("[net] homebrew fetch HTTP 200 but 0 ids parsed — falling back to cache\n");
    } else {
        if (status < 0)
            printf("[net] homebrew fetch failed (transport error) — using SD cache\n");
        else
            printf("[net] homebrew fetch status=%d — using SD cache\n", status);
    }

    // Try SD cache.
    if (!load_cache(s_ids)) {
        printf("[titles] no homebrew cache — showing all titles (fail-open)\n");
        s_ids.clear();
    }
}

bool homebrew::is_homebrew(uint64_t tid) {
    return std::binary_search(s_ids.begin(), s_ids.end(), tid);
}
