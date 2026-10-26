package dev.waystone.data.sync

import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PlanAction
import uniffi.waystone_mobile.SyncPhase
import uniffi.waystone_mobile.SyncPlanEntry
import uniffi.waystone_mobile.SyncSummary
import uniffi.waystone_mobile.syncSummaryHeadline

/** A ROM slot that exists only on the server (no local file yet); restored, never pushed. */
val NormalizedSave.isRemoteOnly: Boolean get() = files.isEmpty()

val SyncPlanEntry.status: SourceStatus
    get() = when (action) {
        PlanAction.PUSH -> SourceStatus.Ahead
        PlanAction.PULL -> SourceStatus.Behind
        PlanAction.IN_SYNC -> SourceStatus.InSync
        PlanAction.CONFLICT -> SourceStatus.Conflict
        PlanAction.ERROR -> SourceStatus.Error
    }

fun SyncSummary.headline(): String = syncSummaryHeadline(this)

fun failedSummary(message: String) = SyncSummary(
    pushed = 0u, pulled = 0u, inSync = 0u, conflicts = 0u, errors = 1u, errorMessages = listOf(message), cancelled = false,
)

sealed interface SyncProgress {
    data class Started(val total: Int) : SyncProgress
    data class Item(val index: Int, val total: Int, val groupKey: String, val displayName: String, val phase: SyncPhase) : SyncProgress
    data class Done(val summary: SyncSummary) : SyncProgress
}
