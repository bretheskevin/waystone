#include "sync_summary.h"
#include <cstdio>

TitleState final_title_state(const TitleTally& t) {
    if (t.failed) return TitleState::Failed;
    if (t.conflict) return TitleState::Conflict;
    if (t.uploaded && t.downloaded) return TitleState::UpDown;
    if (t.downloaded) return TitleState::Downloaded;
    if (t.uploaded) return TitleState::Uploaded;
    return TitleState::InSync;
}

const char* title_state_label(TitleState s) {
    switch (s) {
    case TitleState::Pending:    return "Waiting";
    case TitleState::Active:     return "Syncing\xe2\x80\xa6";
    case TitleState::InSync:     return "In sync";
    case TitleState::Uploaded:   return "Uploaded";
    case TitleState::Downloaded: return "Downloaded";
    case TitleState::UpDown:     return "Uploaded + downloaded";
    case TitleState::Conflict:   return "Conflict \xe2\x80\x94 resolve manually";
    case TitleState::Failed:     return "Failed";
    }
    return "";
}

static void append_part(std::string& out, bool& any, int count, const char* word) {
    if (count <= 0) return;
    char part[48];
    snprintf(part, sizeof(part), "%s%d %s", any ? ", " : "", count, word);
    out += part;
    any = true;
}

std::string format_sync_headline(const std::vector<TitleResult>& results) {
    int synced = 0, up = 0, down = 0, conflicts = 0, failed = 0;
    for (size_t i = 0; i < results.size(); i++) {
        switch (results[i].state) {
        case TitleState::InSync:     synced++; break;
        case TitleState::Uploaded:   up++; break;
        case TitleState::Downloaded: down++; break;
        case TitleState::UpDown:     up++; down++; break;
        case TitleState::Conflict:   conflicts++; break;
        case TitleState::Failed:     failed++; break;
        default: break;
        }
    }
    std::string out = "Done \xe2\x80\x94 ";
    bool any = false;
    append_part(out, any, synced, "synced");
    append_part(out, any, up, "uploaded");
    append_part(out, any, down, "downloaded");
    append_part(out, any, conflicts, conflicts == 1 ? "conflict" : "conflicts");
    append_part(out, any, failed, "failed");
    if (!any) out += "nothing to sync";
    return out;
}

float combined_progress(int index, size_t n, size_t got, size_t total) {
    if (n == 0) return 0.0f;
    if (index < 0) index = 0;
    float frac = 0.0f;
    if (total > 0) {
        frac = (float)got / (float)total;
        if (frac > 1.0f) frac = 1.0f;
    }
    float p = ((float)index + frac) / (float)n;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

size_t next_xfer_total(size_t prev, size_t reported) {
    return reported > 0 ? reported : prev;
}

bool put_needs_parent_dir(int put_rc) {
    return put_rc == 409 || put_rc == 404;
}

std::string href_last_segment(const std::string& href) {
    std::string h = href;
    while (!h.empty() && h[h.size() - 1] == '/') h.erase(h.size() - 1);
    size_t slash = h.rfind('/');
    return (slash == std::string::npos) ? h : h.substr(slash + 1);
}
