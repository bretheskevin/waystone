#ifndef WAYSTONE_UPDATER_H
#define WAYSTONE_UPDATER_H

#include <stddef.h>

// Result codes for the self-updater. Distinct so the UI can tell "offline"
// apart from "GitHub rejected us" and "we were netload-launched".
enum UpdaterRc {
    UP_OK = 0,
    UP_NET,       // transport failure / no connection
    UP_GH,        // GitHub answered HTTP but not 200 (rate-limited, repo error)
    UP_PARSE,     // response body was not the expected JSON
    UP_NO_ASSET,  // latest release has no .3dsx asset
    UP_NO_SELF,   // own .3dsx not found on SD card (e.g. netload-launched)
    UP_IO,        // local file IO failure (write/rename)
};

// Records argv[0] at startup if it points into the SD card (sdmc:). No-op otherwise.
void updater_set_argv0(const char* argv0);

// Fetch the latest GitHub release and extract the version tag (leading 'v' stripped)
// into out_ver, plus the .3dsx asset download URL into out_asset_url. Returns an
// UpdaterRc; out_* are untouched on failure.
int updater_check_latest(char* out_ver, size_t ver_sz,
                         char* out_asset_url, size_t url_sz);

// Parse a GitHub /releases/latest JSON body (split out for host testing).
// Same outputs and return codes as updater_check_latest.
int updater_parse_release_json(const char* json, char* out_ver, size_t ver_sz,
                               char* out_asset_url, size_t url_sz);

// Numeric compare of dot-separated version components (1.10.0 > 1.9.0).
// Malformed/missing components compare as 0. true only when latest > current.
bool version_newer(const char* latest, const char* current);

// Resolve the path of the running .3dsx: captured argv[0] if sdmc:, else the
// default install location. Probes the file — UP_NO_SELF if missing (netload).
int updater_self_path(char* out, size_t out_sz);

// Download the .3dsx from url to "<self_path>.tmp", then rename over self_path.
// progress(got, total, ctx) runs on the worker thread; return false to abort.
// Returns an UpdaterRc; the tmp file is removed on any failure.
int updater_install(const char* url, const char* self_path,
                    bool (*progress)(size_t got, size_t total, void* ctx),
                    void* ctx);

#endif
