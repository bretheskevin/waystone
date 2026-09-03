#[derive(Debug, Clone, Copy, uniffi::Enum)]
pub enum ConflictPolicy {
    NewestWins,
    Prompt,
}

#[derive(Debug, uniffi::Enum)]
pub enum SyncDecision {
    InSync,
    Push,
    Pull {
        head_hash: String,
    },
    ConflictResolved {
        winner: String,
        loser_hash: String,
    },
    ConflictNeedsInput {
        local_hash: String,
        remote_hash: String,
    },
}

#[derive(Debug, uniffi::Enum)]
pub enum PushOutcome {
    Pushed,
    BlobExisted,
}

#[derive(Debug, uniffi::Record)]
pub struct PullOutcome {
    pub decision: SyncDecision,
    pub files: Option<Vec<crate::types::FileEntry>>,
}
