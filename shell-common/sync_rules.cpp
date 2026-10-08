#include "sync_rules.h"
#include "json.h"

#include <ctime>
#include <vector>

static const long long kMinPlausibleMtime = 1483228800LL;  // 2017-01-01T00:00:00Z
static const long long kMaxFutureSkewSecs = 86400LL;

SyncAct sync_act_for(const std::string& type, const std::string& winner) {
    if (type.empty()) return SyncAct::ScanFailed;
    if (type == "in_sync") return SyncAct::None;
    if (type == "push") return SyncAct::Push;
    if (type == "pull") return SyncAct::Pull;
    if (type == "conflict_needs_input") return SyncAct::Conflict;
    if (type == "conflict_resolved") {
        if (winner == "local") return SyncAct::Push;
        if (winner == "remote") return SyncAct::Pull;
    }
    return SyncAct::Unknown;
}

int effective_policy(int cfg_policy, bool have_local_mtime) {
    if (cfg_policy == 0 && !have_local_mtime) return 1;
    return cfg_policy;
}

HeadsOutcome classify_heads(size_t json_hrefs, size_t decrypted, bool have_local) {
    if (json_hrefs == 0) return have_local ? HeadsOutcome::Push : HeadsOutcome::InSync;
    if (decrypted == 0) return HeadsOutcome::Failed;
    return HeadsOutcome::Decide;
}

bool is_head_href(const std::string& href) {
    return href.size() >= 5 && href.compare(href.size() - 5, 5, ".json") == 0;
}

std::string utc_iso_from_epoch(long long secs) {
    time_t t = (time_t)secs;
    struct tm tmv;
    if (!gmtime_r(&t, &tmv)) return "";
    char buf[32];
    if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) return "";
    return buf;
}

std::string plausible_mtime_iso(long long file_secs, long long now_secs) {
    if (file_secs < kMinPlausibleMtime) return "";
    if (file_secs > now_secs + kMaxFutureSkewSecs) return "";
    return utc_iso_from_epoch(file_secs);
}

std::string own_head_hash(const std::string& heads_array, const std::string& device_id) {
    std::vector<std::string> heads = json_split_array(heads_array.c_str());
    for (size_t i = 0; i < heads.size(); i++) {
        if (json_get_string(heads[i].c_str(), "device_id") == device_id)
            return json_get_string(heads[i].c_str(), "hash");
    }
    return "";
}

bool base_needs_repair(const std::string& decision_type, const std::string& own_hash,
                       const std::string& local_hash) {
    return decision_type == "in_sync" && !local_hash.empty() && own_hash != local_hash;
}
