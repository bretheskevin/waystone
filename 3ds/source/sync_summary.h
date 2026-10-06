#ifndef WAYSTONE_3DS_SYNC_SUMMARY_H
#define WAYSTONE_3DS_SYNC_SUMMARY_H

#include <cstddef>
#include <string>
#include <vector>

enum class TitleState { Pending, Active, InSync, Uploaded, Downloaded, UpDown, Conflict, Failed };

struct TitleResult {
    TitleState  state;
    std::string reason;   // short user-facing failure reason; empty unless Failed
    TitleResult() : state(TitleState::Pending) {}
};

// Per-title outcome accumulated across the push pass and the pull pass.
struct TitleTally {
    bool push_failed;
    bool pull_failed;
    bool uploaded;
    bool downloaded;
    bool conflict;
    std::string reason;
    TitleTally()
        : push_failed(false), pull_failed(false), uploaded(false),
          downloaded(false), conflict(false) {}
};

// Precedence: Failed > Conflict > UpDown > Downloaded > Uploaded > InSync.
TitleState final_title_state(const TitleTally& t);

// Short user-facing label (also used in logs).
const char* title_state_label(TitleState s);

// "Done — N synced, U uploaded, D downloaded, C conflicts, F failed" with zero parts omitted;
// UpDown counts toward both uploaded and downloaded. Empty input -> "Done — nothing to sync".
std::string format_sync_headline(const std::vector<TitleResult>& results);

// Combined 0..1 bar across both passes: (pass*n + index + got/total) / (2n). Clamped.
float combined_progress(int pass, int index, size_t n, size_t got, size_t total);

// curl reports total 0 until it knows the size (e.g. while connecting): keep the previous total then.
size_t next_xfer_total(size_t prev, size_t reported);

// webdav_put rc meaning "parent collection missing": 409 Conflict (RFC 4918), or 404 on some servers.
bool put_needs_parent_dir(int put_rc);

#endif
