#ifndef WAYSTONE_SYNC_RULES_H
#define WAYSTONE_SYNC_RULES_H

#include <cstddef>
#include <string>

// Pure rules of the decide-first sync engine (no FFI, no network; host-tested).

enum class SyncAct { None, Push, Pull, Conflict, ScanFailed, Unknown };

// Map a core SyncDecision ("type" + "winner") to the engine action. "" = server check failed.
SyncAct sync_act_for(const std::string& decision_type, const std::string& winner);

// NewestWins needs a real local timestamp; without one the save is treated as Prompt
// (skipped + counted as a conflict) so it is never overwritten on a guess.
int effective_policy(int cfg_policy, bool have_local_mtime);

enum class HeadsOutcome { Decide, Push, InSync, Failed };

// json_hrefs: head files listed by PROPFIND; decrypted: heads fetched and decrypted.
HeadsOutcome classify_heads(size_t json_hrefs, size_t decrypted, bool have_local);

bool is_head_href(const std::string& href);

// "YYYY-MM-DDTHH:MM:SSZ" — same format as current_utc_time(), so lexical compare == time compare.
std::string utc_iso_from_epoch(long long secs);

// "" when the file mtime is unusable (before 2017-01-01 or > 1 day ahead of now_secs).
std::string plausible_mtime_iso(long long file_secs, long long now_secs);

// Hash of this device's own head in a decrypted heads JSON array; "" when absent.
std::string own_head_hash(const std::string& heads_array, const std::string& device_id);

// In-sync save whose own head is missing/stale: rewrite it so a later local-only edit is a
// clean push, not a false conflict (also heals bases left stale by older builds).
bool base_needs_repair(const std::string& decision_type, const std::string& own_hash,
                       const std::string& local_hash);

#endif
