#include "updater.h"
#include "net.h"
#define JSMN_HEADER
#include "jsmn.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Engine TU: no citro2d, no <3ds.h> — stays host-compilable and CONSOLE=1-clean.

static const char* kLatestReleaseUrl =
    "https://api.github.com/repos/bretheskevin/waystone/releases/latest";
static const char* kFallbackSelfPath = "sdmc:/3ds/waystone/waystone-3ds-spike.3dsx";

// A /releases/latest body has ~10 top-level keys plus one small object per asset;
// 4096 tokens is far beyond worst case (heap, not the 3DS worker stack).
static const int UPDATE_MAX_TOKENS = 4096;

static char g_argv0[512];
static bool g_have_argv0 = false;

void updater_set_argv0(const char* argv0) {
    printf("[update] set_argv0: %s\n", argv0 ? argv0 : "(null)");
    if (argv0 && strncmp(argv0, "sdmc:", 5) == 0) {
        snprintf(g_argv0, sizeof(g_argv0), "%s", argv0);
        g_have_argv0 = true;
        printf("[update] argv0 captured: %s\n", g_argv0);
    } else {
        printf("[update] argv0 not on SD (netload?), self-update will need relaunch\n");
    }
}

// ---- version compare ----

// Parse a numeric component at *p; malformed/empty parses as 0. *p advances
// past the digits (the caller skips the '.').
static long ver_component(const char** p) {
    long v = 0;
    bool any = false;
    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        any = true;
        (*p)++;
    }
    return any ? v : 0;
}

bool version_newer(const char* latest, const char* current) {
    const char* l = latest ? latest : "";
    const char* c = current ? current : "";
    for (;;) {
        long lv = ver_component(&l);
        long cv = ver_component(&c);
        if (lv > cv) return true;
        if (lv < cv) return false;
        if (*l == '\0' && *c == '\0') return false;
        // Skip one separator char per side ('.' or any stray char); an exhausted
        // side keeps yielding component 0, so "1.0" == "1.0.0".
        if (*l) l++;
        if (*c) c++;
    }
}

// ---- release JSON parsing (jsmn) ----

static bool tok_eq(const char* json, const jsmntok_t& tok, const char* key) {
    if (tok.type != JSMN_STRING) return false;
    size_t len = static_cast<size_t>(tok.end - tok.start);
    return (strlen(key) == len && strncmp(json + tok.start, key, len) == 0);
}

static int skip_token(const jsmntok_t* tokens, int idx, int total) {
    if (idx >= total) return total;
    if (tokens[idx].type == JSMN_OBJECT) {
        int children = tokens[idx].size;
        int cur = idx + 1;
        for (int k = 0; k < children && cur < total; k++) {
            cur = skip_token(tokens, cur, total);  // key
            cur = skip_token(tokens, cur, total);  // value
        }
        return cur;
    }
    if (tokens[idx].type == JSMN_ARRAY) {
        int children = tokens[idx].size;
        int cur = idx + 1;
        for (int k = 0; k < children && cur < total; k++) {
            cur = skip_token(tokens, cur, total);
        }
        return cur;
    }
    return idx + 1;  // STRING or PRIMITIVE
}

static bool ends_with_3dsx(const char* s) {
    size_t len = strlen(s);
    return len >= 5 && strncmp(s + len - 5, ".3dsx", 5) == 0;
}

int updater_parse_release_json(const char* json, char* out_ver, size_t ver_sz,
                               char* out_asset_url, size_t url_sz) {
    std::vector<jsmntok_t> tokens(UPDATE_MAX_TOKENS);
    jsmn_parser parser;
    jsmn_init(&parser);
    int n = jsmn_parse(&parser, json, strlen(json), tokens.data(), UPDATE_MAX_TOKENS);
    if (n < 2 || tokens[0].type != JSMN_OBJECT) {
        printf("[update] parse_release: not a JSON object (tokens=%d)\n", n);
        return UP_PARSE;
    }

    bool have_ver = false;
    bool have_asset = false;
    char ver[64];
    char url[768];

    int top_children = tokens[0].size;
    int i = 1;
    for (int t = 0; t < top_children && i < n - 1; t++) {
        bool is_tag = tok_eq(json, tokens[i], "tag_name");
        bool is_assets = tok_eq(json, tokens[i], "assets");
        int val = i + 1;
        if (is_tag && val < n && tokens[val].type == JSMN_STRING) {
            const char* start = json + tokens[val].start;
            size_t len = static_cast<size_t>(tokens[val].end - tokens[val].start);
            if (len > 0 && start[0] == 'v') { start++; len--; }
            if (len >= sizeof(ver)) len = sizeof(ver) - 1;
            memcpy(ver, start, len);
            ver[len] = '\0';
            have_ver = true;
        } else if (is_assets && val < n && tokens[val].type == JSMN_ARRAY) {
            int assets = tokens[val].size;
            int a = val + 1;
            for (int ac = 0; ac < assets && a < n; ac++) {
                if (tokens[a].type == JSMN_OBJECT) {
                    int fields = tokens[a].size;
                    int f = a + 1;
                    for (int fc = 0; fc < fields && f < n - 1; fc++) {
                        if (tok_eq(json, tokens[f], "browser_download_url") &&
                            tokens[f + 1].type == JSMN_STRING) {
                            const char* start = json + tokens[f + 1].start;
                            size_t len = static_cast<size_t>(
                                tokens[f + 1].end - tokens[f + 1].start);
                            if (len < sizeof(url)) {
                                memcpy(url, start, len);
                                url[len] = '\0';
                                if (ends_with_3dsx(url)) have_asset = true;
                            }
                        }
                        f++;                                  // skip key
                        f = skip_token(tokens.data(), f, n);  // skip value
                    }
                }
                a = skip_token(tokens.data(), a, n);
            }
        }
        i++;                                  // skip key
        i = skip_token(tokens.data(), i, n);  // skip value
    }

    if (!have_ver) {
        printf("[update] parse_release: no tag_name\n");
        return UP_PARSE;
    }
    if (!have_asset) {
        printf("[update] parse_release: no .3dsx asset (ver=%s)\n", ver);
        return UP_NO_ASSET;
    }
    snprintf(out_ver, ver_sz, "%s", ver);
    snprintf(out_asset_url, url_sz, "%s", url);
    printf("[update] parse_release: ver=%s url=%.48s\n", out_ver, out_asset_url);
    return UP_OK;
}

// ---- network check ----

int updater_check_latest(char* out_ver, size_t ver_sz,
                         char* out_asset_url, size_t url_sz) {
    printf("[update] check_latest: GET %s\n", kLatestReleaseUrl);
    std::string json;
    int rc = http_get(kLatestReleaseUrl, &json);
    if (rc == -1) {
        printf("[update] check_latest: transport error (offline?)\n");
        return UP_NET;
    }
    if (rc != 200) {
        printf("[update] check_latest: GitHub HTTP %d\n", rc);
        return UP_GH;
    }
    return updater_parse_release_json(json.c_str(), out_ver, ver_sz,
                                      out_asset_url, url_sz);
}

// ---- self path + install ----

int updater_self_path(char* out, size_t out_sz) {
    const char* path = g_have_argv0 ? g_argv0 : kFallbackSelfPath;
    printf("[update] self_path: probing %s (argv0 %s)\n",
           path, g_have_argv0 ? "captured" : "not captured, fallback");
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("[update] self_path: %s not readable — relaunch from SD to enable updates\n",
               path);
        return UP_NO_SELF;
    }
    fclose(f);
    snprintf(out, out_sz, "%s", path);
    return UP_OK;
}

int updater_install(const char* url, const char* self_path,
                    bool (*progress)(size_t got, size_t total, void* ctx),
                    void* ctx) {
    printf("[update] install: url=%.48s self=%s\n",
           url ? url : "(null)", self_path ? self_path : "(null)");
    if (!self_path || !*self_path) {
        printf("[update] install: no self path\n");
        return UP_NO_SELF;
    }
    FILE* probe = fopen(self_path, "rb");
    if (!probe) {
        printf("[update] install: %s not found\n", self_path);
        return UP_NO_SELF;
    }
    fclose(probe);

    std::string tmp = std::string(self_path) + ".tmp";
    int rc = http_download(url, tmp.c_str(), progress, ctx);
    if (rc != 0) {
        // http_download already removed tmp on any failure (documented contract).
        printf("[update] install: download FAILED rc=%d\n", rc);
        return UP_NET;
    }
    if (rename(tmp.c_str(), self_path) != 0) {
        printf("[update] install: rename %s -> %s FAILED\n",
               tmp.c_str(), self_path);
        remove(tmp.c_str());
        return UP_IO;
    }
    printf("[update] install: %s updated successfully\n", self_path);
    return UP_OK;
}
