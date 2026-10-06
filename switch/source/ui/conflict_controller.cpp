#include "conflict_controller.h"
#include "session.h"
#include "sync.h"
#include "json.h"
#include <cstdio>

extern "C" {
#include "waystone.h"
}

ConflictController::ConflictController(WsVault* vault, AccountUid uid, std::string device_id,
                                       WebDavCfg dav, std::vector<TitleInfo> titles)
    : vault_(vault), uid_(uid), device_id_(std::move(device_id)),
      dav_url_(dav.base_url), dav_user_(dav.user), dav_pass_(dav.pass),
      dav_{dav_url_.c_str(), dav_user_.c_str(), dav_pass_.c_str()},
      titles_(std::move(titles)) {}

ConflictController::~ConflictController() { join(); zeroize_string(dav_pass_); }

void ConflictController::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    if (thread_.joinable()) thread_.join();
    phase_.store(ConflictPhase::Scanning);
    { std::lock_guard<std::mutex> lk(mu_); status_ = "Scanning for conflicts..."; conflicts_.clear(); }
    thread_ = std::thread(&ConflictController::scan_worker, this);
}

void ConflictController::join() { if (thread_.joinable()) thread_.join(); }
ConflictPhase ConflictController::phase() const { return phase_.load(); }
std::string ConflictController::status() const { std::lock_guard<std::mutex> lk(mu_); return status_; }
std::vector<ConflictItem> ConflictController::conflicts() const { std::lock_guard<std::mutex> lk(mu_); return conflicts_; }

void ConflictController::resolve_keep_local(size_t index)  { resolve_impl(true,  index); }
void ConflictController::resolve_keep_remote(size_t index) { resolve_impl(false, index); }

void ConflictController::resolve_impl(bool keep_local, size_t index) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    ConflictItem item;
    { std::lock_guard<std::mutex> lk(mu_);
      if (index >= conflicts_.size()) { running_.store(false); return; }
      item = conflicts_[index]; }
    if (thread_.joinable()) thread_.join();
    phase_.store(ConflictPhase::Resolving);
    { std::lock_guard<std::mutex> lk(mu_);
      status_ = (keep_local ? "Pushing local: " : "Restoring remote: ") + item.group_key; }
    thread_ = std::thread(&ConflictController::resolve_worker, this, keep_local, std::move(item), index);
}

void ConflictController::resolve_worker(bool keep_local, ConflictItem item, size_t index) {
    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[conflict] webdav_session_begin failed\n");
        { std::lock_guard<std::mutex> lk(mu_);
          status_ = "Network init failed";
          phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready); }
        running_.store(false);
        return;
    }
    int rc;
    if (keep_local) {
        TitleInfo ti; ti.title_id = item.title_id; ti.name = item.title_name;
        rc = push_title(vault_, ti, uid_, device_id_.c_str(), sess);
    } else {
        rc = restore_remote_save(vault_, item.remote_hash, item.base_path,
                                 item.group_key, item.raw_tree, item.title_id, uid_, sess);
    }
    webdav_session_end(sess);
    { std::lock_guard<std::mutex> lk(mu_);
      bool ok = keep_local ? (rc >= 0) : (rc == 0);
      if (ok) {
          if (index < conflicts_.size())
              conflicts_.erase(conflicts_.begin() + static_cast<long>(index));
          status_ = (keep_local ? "Resolved (kept local): " : "Resolved (kept remote): ") + item.group_key;
      } else {
          status_ = (keep_local ? "Error pushing: " : "Error restoring: ") + item.group_key;
      }
      phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready);
    }
    running_.store(false);
}

void ConflictController::scan_worker() {
    char buf[256];
    const size_t n = titles_.size();
    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[conflict] webdav_session_begin failed\n");
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Network init failed"; }
        phase_.store(ConflictPhase::Error);
        running_.store(false);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "Scanning %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
        { std::lock_guard<std::mutex> lk(mu_); status_ = buf; }
        std::vector<SaveDecision> decisions = scan_title(vault_, titles_[i], uid_,
                                                         device_id_.c_str(),
                                                         1 /* Prompt */, sess);
        for (const auto& d : decisions) {
            if (d.decision_type != "conflict_needs_input") continue;
            std::string remote_hash, remote_device_id, remote_mtime;
            char* folded = ws_fold_heads(d.heads_array.c_str());
            if (folded) {
                remote_hash = json_get_string(folded, "hash");
                remote_device_id = json_get_string(folded, "device_id");
                remote_mtime = json_get_string(folded, "mtime");
                ws_string_free(folded);
            }
            ConflictItem ci;
            ci.title_name = titles_[i].name; ci.title_id = titles_[i].title_id; ci.uid = uid_;
            ci.group_key = d.group_key; ci.local_hash = d.local_hash; ci.local_mtime = d.mtime;
            ci.remote_hash = remote_hash; ci.remote_device_id = remote_device_id; ci.remote_mtime = remote_mtime;
            ci.base_path = d.base_path; ci.heads_array = d.heads_array; ci.raw_tree = d.raw_tree;
            { std::lock_guard<std::mutex> lk(mu_); conflicts_.push_back(std::move(ci)); }
        }
    }
    webdav_session_end(sess);
    size_t found;
    { std::lock_guard<std::mutex> lk(mu_); found = conflicts_.size();
      snprintf(buf, sizeof(buf), "Scan complete: %zu conflict(s) found", found); status_ = buf; }
    phase_.store(found > 0 ? ConflictPhase::Ready : ConflictPhase::Done);
    running_.store(false);
}
