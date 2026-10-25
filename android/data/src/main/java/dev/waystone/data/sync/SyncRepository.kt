package dev.waystone.data.sync

import java.util.logging.Level
import java.util.logging.Logger
import dev.waystone.data.roms.RemoteOnlyProbe
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.DeviceHead
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.HistoryEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PushOutcome
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.WaystoneException
import uniffi.waystone_mobile.decidePull as ffiDecidePull
import uniffi.waystone_mobile.fetchBlob as ffiFetchBlob
import uniffi.waystone_mobile.foldHeads as ffiFoldHeads
import uniffi.waystone_mobile.listHistory as ffiListHistory
import uniffi.waystone_mobile.localHash as ffiLocalHash
import uniffi.waystone_mobile.pushOne as ffiPushOne
import uniffi.waystone_mobile.readRemoteHeads as ffiReadRemoteHeads

enum class SourceStatus(val rank: Int) {
    InSync(0),
    Ahead(1),
    Behind(2),
    Conflict(3),
    Error(4);

    companion object {
        fun worst(statuses: Iterable<SourceStatus>): SourceStatus =
            statuses.maxByOrNull { it.rank } ?: InSync
    }
}

data class ConflictEntry(
    val save: NormalizedSave,
    val localHash: String,
    val remoteHash: String,
    val remoteDeviceId: String,
    val remoteMtime: String,
)

/** Mirrors desktop/src/tui/action.rs sync flows over the UniFFI binding. */
class SyncRepository(
    private val vault: Vault,
    val dav: WebDav,
    private val deviceId: String,
    private val io: CoroutineContext = Dispatchers.IO,
) {

    private val log = Logger.getLogger("WS:SYNC")

    fun remoteOnlyProbe(): RemoteOnlyProbe = RemoteOnlyProbe(vault, dav)

    suspend fun hashOf(save: NormalizedSave): String = withContext(io) { ffiLocalHash(save) }

    /** Newest head from another device, for inbox display on ConflictNeedsInput. */
    fun conflictEntry(
        save: NormalizedSave,
        heads: List<DeviceHead>,
        decision: SyncDecision.ConflictNeedsInput,
    ): ConflictEntry? {
        val other = heads.filter { it.deviceId != deviceId }.maxByOrNull { it.mtime }
        if (other == null) {
            log.warning("no foreign head for conflict on ${save.groupKey}")
            return null
        }
        return ConflictEntry(save, decision.localHash, decision.remoteHash, other.deviceId, other.mtime)
    }

    suspend fun pushOne(save: NormalizedSave): PushOutcome = withContext(io) {
        ffiPushOne(vault, save, deviceId, dav).also { log.info("push ${save.groupKey} -> $it") }
    }

    /** fetch_blob already returns decrypted plaintext zip bytes — no decryptBlob here. */
    suspend fun keepRemoteFiles(save: NormalizedSave, remoteHash: String): List<FileEntry> =
        withContext(io) {
            val zip = ffiFetchBlob(vault, save, remoteHash, dav)
            log.info("fetched remote blob for ${save.groupKey} (${zip.size}B)")
            CanonicalUnzip.unzip(zip)
        }

    suspend fun history(save: NormalizedSave): List<HistoryEntry> = withContext(io) {
        ffiListHistory(vault, save, dav).also { log.info("history ${save.groupKey}: ${it.size} entries") }
    }

    suspend fun planOne(save: NormalizedSave, policy: ConflictPolicy): SyncPlanEntry = withContext(io) {
        runCatchingCancellable {
            val local = if (save.isRemoteOnly) null else ffiLocalHash(save)
            val heads = ffiReadRemoteHeads(vault, save, dav)
            val d = ffiDecidePull(local, save.mtime, heads, deviceId, policy)
            val entry = when (d) {
                SyncDecision.InSync -> SyncPlanEntry(save, PlanAction.InSync, d, heads = heads)
                SyncDecision.Push -> SyncPlanEntry(save, PlanAction.Push, d, heads = heads)
                is SyncDecision.Pull -> SyncPlanEntry(save, PlanAction.Pull, d, d.headHash, heads)
                is SyncDecision.ConflictResolved ->
                    if (d.winner == "remote") {
                        val hash = ffiFoldHeads(heads)?.hash
                        if (hash == null) SyncPlanEntry(save, PlanAction.Error, d, heads = heads, error = "no remote head to pull")
                        else SyncPlanEntry(save, PlanAction.Pull, d, hash, heads)
                    } else {
                        SyncPlanEntry(save, PlanAction.Push, d, heads = heads)
                    }
                is SyncDecision.ConflictNeedsInput -> SyncPlanEntry(save, PlanAction.Conflict, d, heads = heads)
            }
            log.info("plan ${save.groupKey}: ${entry.action} (decision=$d, remoteOnly=${save.isRemoteOnly})")
            entry
        }.getOrElse { e ->
            log.log(Level.WARNING, "plan failed for ${save.groupKey}", e)
            SyncPlanEntry(save, PlanAction.Error, error = e.message ?: e.javaClass.simpleName)
        }
    }

    suspend fun plan(saves: List<NormalizedSave>, policy: ConflictPolicy): SyncPlan {
        val started = System.currentTimeMillis()
        log.info("plan start: ${saves.size} saves, policy=$policy")
        return SyncPlan(saves.map { planOne(it, policy) }).also {
            log.info("plan done in ${System.currentTimeMillis() - started} ms: ${it.describe()}")
        }
    }

    /**
     * Acts on a decided plan, mirroring shell-common sync_engine: never aborts, every failure lands in the
     * summary. [restore] writes pulled files (guarded restore); false = aborted (e.g. safety snapshot failed).
     * [isCancelled] is checked before each item, so a cancel takes effect after the current save.
     */
    fun execute(
        plan: SyncPlan,
        restore: suspend (SyncPlanEntry, List<FileEntry>) -> Boolean,
        isCancelled: () -> Boolean = { false },
    ): Flow<SyncProgress> = flow {
        val total = plan.entries.size
        emit(SyncProgress.Started(total))
        log.info("execute start: $total entries (${plan.describe()})")
        var s = SyncSummary()
        val errors = mutableListOf<String>()
        fun fail(entry: SyncPlanEntry, why: String, cause: Throwable? = null) {
            log.log(Level.WARNING, "execute ${entry.save.groupKey} failed: $why", cause)
            errors += "${entry.save.displayName}: $why"
            s = s.copy(errors = s.errors + 1)
        }
        for ((index, entry) in plan.entries.withIndex()) {
            if (isCancelled()) {
                log.info("execute cancelled before item ${index + 1}/$total")
                s = s.copy(cancelled = true)
                break
            }
            when (entry.action) {
                PlanAction.InSync -> s = s.copy(inSync = s.inSync + 1)
                PlanAction.Conflict -> s = s.copy(conflicts = s.conflicts + 1)
                PlanAction.Error -> fail(entry, entry.error ?: "unknown error")
                PlanAction.Push -> {
                    emit(SyncProgress.Item(index, total, entry.save, SyncPhase.Pushing))
                    runCatchingCancellable { pushOne(entry.save) }
                        .onSuccess { s = s.copy(pushed = s.pushed + 1) }
                        .onFailure { e -> fail(entry, e.message ?: "push failed", e) }
                }
                PlanAction.Pull -> {
                    emit(SyncProgress.Item(index, total, entry.save, SyncPhase.Pulling))
                    val hash = entry.pullHash
                    val files = if (hash == null) {
                        fail(entry, "no head hash"); null
                    } else {
                        runCatchingCancellable { keepRemoteFiles(entry.save, hash) }
                            .onFailure { e -> fail(entry, e.message ?: "download failed", e) }
                            .getOrNull()
                    }
                    if (files != null) {
                        emit(SyncProgress.Item(index, total, entry.save, SyncPhase.Restoring))
                        val restored = runCatchingCancellable { restore(entry, files) }
                        if (restored.getOrDefault(false)) s = s.copy(pulled = s.pulled + 1)
                        else fail(entry, "restore aborted", restored.exceptionOrNull())
                    }
                }
            }
        }
        val summary = s.copy(errorMessages = errors.take(SyncSummary.MAX_ERROR_MESSAGES))
        log.info("execute done: ${summary.headline()}")
        emit(SyncProgress.Done(summary))
    }

    /**
     * Safety snapshot FIRST (abort on failure when enabled), then write, then
     * prune — mirrors desktop helpers::guarded_restore ordering.
     * Snapshot ops are injected so app-layer callers can route them through their
     * provider seam instead of a concrete SnapshotStore.
     */
    suspend fun guardedRestore(
        save: NormalizedSave,
        files: List<FileEntry>,
        write: (List<FileEntry>) -> Int,
        currentLocalFiles: () -> List<FileEntry>,
        takeSnapshot: suspend (key: String, files: Map<String, ByteArray>) -> Boolean,
        prune: suspend (key: String) -> Unit,
        safetyBackup: Boolean,
    ): Boolean {
        if (safetyBackup) {
            val current = currentLocalFiles().associate { it.path to it.content }
            if (current.isEmpty()) {
                log.info("no local files for ${save.groupKey}; safety snapshot skipped")
            } else if (!takeSnapshot(save.groupKey, current)) {
                log.warning("safety snapshot failed for ${save.groupKey}; restore aborted")
                return false
            }
        }
        val written = write(files)
        log.info("restored ${save.groupKey}: $written/${files.size} files written")
        try {
            prune(save.groupKey)
        } catch (e: Exception) {
            log.log(Level.WARNING, "prune failed for ${save.groupKey} (non-fatal)", e)
        }
        return true
    }
}
