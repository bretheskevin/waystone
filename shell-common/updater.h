#ifndef WAYSTONE_UPDATER_H
#define WAYSTONE_UPDATER_H

#include <stddef.h>
#include <string>

// Result codes for the self-updater. Distinct so the UI can tell "offline"
// apart from "GitHub rejected us" and "we were netload-launched".
enum UpdaterRc {
    UP_OK = 0,
    UP_NET,       // transport failure / no connection
    UP_GH,        // GitHub answered HTTP but not 200 (rate-limited, repo error)
    UP_PARSE,     // response body was not the expected JSON
    UP_NO_ASSET,  // latest release has no asset with the requested suffix
    UP_NO_SELF,   // own binary not found on SD card (e.g. netload-launched)
    UP_IO,        // local file IO failure (write/rename)
};

// argv0 is used when it points into the SD card ("sdmc:"); otherwise fallback_path
// (the shell's default install location).
void updater_set_self_candidates(const char* argv0, const char* fallback_path);

// Fetch the latest GitHub release: version tag (leading 'v' stripped) into out_ver and
// the first asset whose URL ends with asset_suffix into out_asset_url. Returns an
// UpdaterRc; out_* are untouched on failure.
int updater_check_latest(const char* asset_suffix, char* out_ver, size_t ver_sz,
                         char* out_asset_url, size_t url_sz);

// Parse a GitHub /releases/latest JSON body (split out for host testing).
int updater_parse_release_json(const char* json, const char* asset_suffix, char* out_ver,
                               size_t ver_sz, char* out_asset_url, size_t url_sz);

// Numeric compare of dot-separated version components (1.10.0 > 1.9.0).
// Malformed/missing components compare as 0. true only when latest > current.
bool version_newer(const char* latest, const char* current);

// Resolve the running binary path: captured argv[0] if sdmc:, else the fallback.
// Probes the file -- UP_NO_SELF if missing (netload) or no candidate set.
int updater_self_path(char* out, size_t out_sz);

// Download to "<self_path>.tmp" then rename over self_path; if that rename fails (FS
// refuses to replace), remove self_path and rename again. progress runs on the worker
// thread; return false to abort. The tmp file is removed on download failure.
int updater_install(const char* url, const char* self_path,
                    bool (*progress)(size_t got, size_t total, void* ctx),
                    void* ctx);

std::string updater_check_message(int rc, const char* asset_suffix);
std::string updater_install_message(int rc);

#endif
