package dev.waystone.app.vm

import dev.waystone.data.sync.SyncProgress
import uniffi.waystone_mobile.SyncPhase
import uniffi.waystone_mobile.SyncSummary

data class SyncRunState(
    val total: Int = 0,
    val index: Int = -1,
    val currentName: String? = null,
    val phase: SyncPhase? = null,
    val cancelling: Boolean = false,
    val summary: SyncSummary? = null,
) {
    val running: Boolean get() = summary == null
    /** 1-based position of the current item, 0 before the first. */
    val position: Int get() = index + 1
    val fraction: Float get() = when {
        summary != null -> 1f
        total == 0 -> 0f
        else -> position.toFloat() / total
    }

    fun reduce(p: SyncProgress): SyncRunState = when (p) {
        is SyncProgress.Started -> copy(total = p.total)
        is SyncProgress.Item -> copy(index = p.index, currentName = p.displayName, phase = p.phase)
        is SyncProgress.Done -> copy(summary = p.summary, phase = null)
    }
}
