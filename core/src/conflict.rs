use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum ConflictPolicy {
    NewestWins,
    Prompt,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConflictWinner {
    Local,
    Remote,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SyncDecision {
    InSync,
    Pull { head_hash: String },
    Push,
    ConflictResolved { winner: ConflictWinner, loser_hash: String },
    ConflictNeedsInput { local_hash: String, remote_hash: String },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceHead {
    pub device_id: String,
    pub hash: String,
    pub mtime: String,
}

#[derive(Debug, Clone)]
pub struct MergedHead {
    pub hash: String,
    pub mtime: String,
}

pub fn fold_heads(heads: &[DeviceHead]) -> Option<MergedHead> {
    let newest = heads
        .iter()
        .max_by(|a, b| a.mtime.cmp(&b.mtime))?;
    Some(MergedHead {
        hash: newest.hash.clone(),
        mtime: newest.mtime.clone(),
    })
}

pub fn three_way_sync(
    local_hash: Option<&str>,
    base_hash: Option<&str>,
    head_hash: Option<&str>,
    local_mtime: &str,
    head_mtime: &str,
    policy: ConflictPolicy,
) -> SyncDecision {
    match (local_hash, base_hash, head_hash) {
        (None, None, None) => SyncDecision::InSync,
        (Some(_), None, None) => SyncDecision::Push,
        (None, None, Some(h)) => SyncDecision::Pull { head_hash: h.into() },
        (Some(l), Some(b), Some(h)) => {
            if l == h {
                SyncDecision::InSync
            } else if l == b {
                SyncDecision::Pull { head_hash: h.into() }
            } else if h == b {
                SyncDecision::Push
            } else {
                resolve_conflict(l, h, local_mtime, head_mtime, policy)
            }
        }
        (Some(_), Some(_), None) => SyncDecision::Push,
        (None, Some(_), Some(h)) => SyncDecision::Pull { head_hash: h.into() },
        (Some(l), None, Some(h)) => {
            if l == h {
                SyncDecision::InSync
            } else {
                resolve_conflict(l, h, local_mtime, head_mtime, policy)
            }
        }
        (None, Some(_), None) => SyncDecision::InSync,
    }
}

fn resolve_conflict(
    local_hash: &str,
    head_hash: &str,
    local_mtime: &str,
    head_mtime: &str,
    policy: ConflictPolicy,
) -> SyncDecision {
    match policy {
        ConflictPolicy::Prompt => SyncDecision::ConflictNeedsInput {
            local_hash: local_hash.into(),
            remote_hash: head_hash.into(),
        },
        ConflictPolicy::NewestWins => {
            if local_mtime >= head_mtime {
                SyncDecision::ConflictResolved {
                    winner: ConflictWinner::Local,
                    loser_hash: head_hash.into(),
                }
            } else {
                SyncDecision::ConflictResolved {
                    winner: ConflictWinner::Remote,
                    loser_hash: local_hash.into(),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn in_sync_when_all_equal() {
        let result = three_way_sync(
            Some("hash_a"),
            Some("hash_a"),
            Some("hash_a"),
            "2026-01-01T00:00:00Z",
            "2026-01-01T00:00:00Z",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::InSync);
    }

    #[test]
    fn remote_advanced_when_local_equals_base() {
        let result = three_way_sync(
            Some("hash_a"),
            Some("hash_a"),
            Some("hash_b"),
            "2026-01-01T00:00:00Z",
            "2026-01-02T00:00:00Z",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::Pull { head_hash: "hash_b".into() });
    }

    #[test]
    fn local_advanced_when_head_equals_base() {
        let result = three_way_sync(
            Some("hash_b"),
            Some("hash_a"),
            Some("hash_a"),
            "2026-01-02T00:00:00Z",
            "2026-01-01T00:00:00Z",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::Push);
    }

    #[test]
    fn conflict_newest_wins_local() {
        let result = three_way_sync(
            Some("hash_b"),
            Some("hash_a"),
            Some("hash_c"),
            "2026-01-03T00:00:00Z",
            "2026-01-02T00:00:00Z",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::ConflictResolved {
            winner: ConflictWinner::Local,
            loser_hash: "hash_c".into(),
        });
    }

    #[test]
    fn conflict_newest_wins_remote() {
        let result = three_way_sync(
            Some("hash_b"),
            Some("hash_a"),
            Some("hash_c"),
            "2026-01-02T00:00:00Z",
            "2026-01-03T00:00:00Z",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::ConflictResolved {
            winner: ConflictWinner::Remote,
            loser_hash: "hash_b".into(),
        });
    }

    #[test]
    fn conflict_prompt_policy() {
        let result = three_way_sync(
            Some("hash_b"),
            Some("hash_a"),
            Some("hash_c"),
            "2026-01-02T00:00:00Z",
            "2026-01-03T00:00:00Z",
            ConflictPolicy::Prompt,
        );
        assert_eq!(result, SyncDecision::ConflictNeedsInput {
            local_hash: "hash_b".into(),
            remote_hash: "hash_c".into(),
        });
    }

    #[test]
    fn new_local_save_no_base_no_head() {
        let result = three_way_sync(
            Some("hash_a"),
            None,
            None,
            "2026-01-01T00:00:00Z",
            "",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::Push);
    }

    #[test]
    fn new_remote_save_no_base_no_local() {
        let result = three_way_sync(
            None,
            None,
            Some("hash_a"),
            "",
            "2026-01-01T00:00:00Z",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(result, SyncDecision::Pull { head_hash: "hash_a".into() });
    }

    #[test]
    fn fold_heads_picks_newest() {
        let heads = vec![
            DeviceHead {
                device_id: "dev1".into(),
                hash: "hash_a".into(),
                mtime: "2026-01-01T00:00:00Z".into(),
            },
            DeviceHead {
                device_id: "dev2".into(),
                hash: "hash_b".into(),
                mtime: "2026-01-03T00:00:00Z".into(),
            },
            DeviceHead {
                device_id: "dev3".into(),
                hash: "hash_a".into(),
                mtime: "2026-01-02T00:00:00Z".into(),
            },
        ];
        let merged = fold_heads(&heads).expect("non-empty heads");
        assert_eq!(merged.hash, "hash_b");
        assert_eq!(merged.mtime, "2026-01-03T00:00:00Z");
    }
}
