package dev.waystone.data.sync

import uniffi.waystone_mobile.DeviceHead
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.SyncDecision

/** A ROM slot that exists only on the server (no local file yet); restored, never pushed. */
val NormalizedSave.isRemoteOnly: Boolean get() = files.isEmpty()

enum class PlanAction { Push, Pull, InSync, Conflict, Error }

data class SyncPlanEntry(
    val save: NormalizedSave,
    val action: PlanAction,
    val decision: SyncDecision? = null,
    /** Blob hash to fetch for [PlanAction.Pull]. */
    val pullHash: String? = null,
    val heads: List<DeviceHead> = emptyList(),
    val error: String? = null,
) {
    val status: SourceStatus
        get() = when (action) {
            PlanAction.Push -> SourceStatus.Ahead
            PlanAction.Pull -> SourceStatus.Behind
            PlanAction.InSync -> SourceStatus.InSync
            PlanAction.Conflict -> SourceStatus.Conflict
            PlanAction.Error -> SourceStatus.Error
        }

    companion object {
        /** Unconditional push (Dashboard "Push" / keep-local). */
        fun forcedPush(save: NormalizedSave): SyncPlanEntry {
            require(!save.isRemoteOnly) { "cannot push remote-only ${save.groupKey}" }
            return SyncPlanEntry(save, PlanAction.Push)
        }
    }
}

data class SyncPlan(val entries: List<SyncPlanEntry>) {
    fun count(action: PlanAction): Int = entries.count { it.action == action }

    fun onlyPulls(): SyncPlan = SyncPlan(entries.filter { it.action == PlanAction.Pull })

    fun describe(): String =
        PlanAction.entries.joinToString(" ") { "${it.name.lowercase()}=${count(it)}" }
}

enum class SyncPhase { Pushing, Pulling, Restoring }

sealed interface SyncProgress {
    data class Started(val total: Int) : SyncProgress
    data class Item(val index: Int, val total: Int, val save: NormalizedSave, val phase: SyncPhase) : SyncProgress
    data class Done(val summary: SyncSummary) : SyncProgress
}

data class SyncSummary(
    val pushed: Int = 0,
    val pulled: Int = 0,
    val inSync: Int = 0,
    val conflicts: Int = 0,
    val errors: Int = 0,
    val errorMessages: List<String> = emptyList(),
    val cancelled: Boolean = false,
) {
    /** Mirrors shell-common format_sync_headline (zero parts omitted). */
    fun headline(): String {
        val parts = listOf(
            inSync to "synced",
            pushed to "uploaded",
            pulled to "downloaded",
            conflicts to if (conflicts == 1) "conflict" else "conflicts",
            errors to "failed",
        ).filter { it.first > 0 }.joinToString(", ") { "${it.first} ${it.second}" }
        val body = parts.ifEmpty { "nothing to sync" }
        return if (cancelled) "Cancelled — $body" else "Done — $body"
    }

    companion object {
        const val MAX_ERROR_MESSAGES = 3
    }
}
