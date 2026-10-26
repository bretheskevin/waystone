package dev.waystone.data.sync

import dev.waystone.data.roms.RemoteOnlyProbe
import java.util.concurrent.atomic.AtomicBoolean
import java.util.logging.Level
import java.util.logging.Logger
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.channels.trySendBlocking
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.buffer
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.HistoryEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PlanAction
import uniffi.waystone_mobile.PushOutcome
import uniffi.waystone_mobile.SaveIo
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.SyncPhase
import uniffi.waystone_mobile.SyncPlanEntry
import uniffi.waystone_mobile.SyncProgressSink
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.engineExecute
import uniffi.waystone_mobile.enginePlan
import uniffi.waystone_mobile.enginePlanOne
import uniffi.waystone_mobile.enginePullHash
import uniffi.waystone_mobile.engineRestoreHash
import uniffi.waystone_mobile.listHistory as ffiListHistory
import uniffi.waystone_mobile.localHash as ffiLocalHash
import uniffi.waystone_mobile.pushOne as ffiPushOne

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
    val headMtime: String,
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
    fun conflictEntry(entry: SyncPlanEntry): ConflictEntry? {
        val d = entry.decision as? SyncDecision.ConflictNeedsInput ?: return null
        val other = entry.heads.filter { it.deviceId != deviceId }.maxByOrNull { it.mtime }
        if (other == null) {
            log.warning("no foreign head for conflict on ${entry.save.groupKey}")
            return null
        }
        return ConflictEntry(entry.save, d.localHash, d.remoteHash, other.deviceId, other.mtime, entry.headMtime)
    }

    suspend fun pushOne(save: NormalizedSave): PushOutcome = withContext(io) {
        ffiPushOne(vault, save, deviceId, dav).also { log.info("push ${save.groupKey} -> $it") }
    }

    /** History restore through [write] (engine restore_hash); this device's head is left untouched. */
    suspend fun restoreHash(
        save: NormalizedSave,
        hash: String,
        write: suspend (List<FileEntry>) -> Boolean,
    ): Boolean = withContext(io) {
        engineRestoreHash(vault, save, hash, dav, saveIo { _, _, files -> write(files) })
            .also { log.info("history restore ${save.groupKey} @ ${hash.take(12)} -> $it") }
    }

    suspend fun history(save: NormalizedSave): List<HistoryEntry> = withContext(io) {
        ffiListHistory(vault, save, dav).also { log.info("history ${save.groupKey}: ${it.size} entries") }
    }

    suspend fun planOne(save: NormalizedSave, policy: ConflictPolicy): SyncPlanEntry = withContext(io) {
        runCatchingCancellable { enginePlanOne(vault, save, deviceId, policy, dav) }.getOrElse { e ->
            log.log(Level.WARNING, "plan failed for ${save.groupKey}", e)
            SyncPlanEntry(
                save = save, action = PlanAction.ERROR, decision = null, localHash = null, ownHeadHash = null,
                pullHash = null, headMtime = "", heads = emptyList(), error = e.message ?: e.javaClass.simpleName,
            )
        }
    }

    /** The engine logs plan start/done (`[sync]`, counts + timing) through the shared log sink. */
    suspend fun plan(saves: List<NormalizedSave>, policy: ConflictPolicy): List<SyncPlanEntry> = withContext(io) {
        enginePlan(vault, saves, deviceId, policy, dav)
    }

    /** Keep-remote: restore through [write] then record this device's head (engine pull_hash). */
    suspend fun keepRemote(
        save: NormalizedSave,
        hash: String,
        headMtime: String,
        write: suspend (List<FileEntry>) -> Boolean,
    ): Boolean = withContext(io) {
        enginePullHash(vault, save, hash, headMtime, deviceId, dav, saveIo { _, _, files -> write(files) })
            .also { log.info("keep-remote ${save.groupKey} -> $it") }
    }

    /**
     * Never aborts; cancel takes effect after the current save (engine semantics). A collector
     * that goes away also cancels: the blocking engine call cannot be interrupted otherwise.
     */
    fun execute(
        plan: List<SyncPlanEntry>,
        write: suspend (index: Int, save: NormalizedSave, files: List<FileEntry>) -> Boolean,
        cancelled: () -> Boolean = { false },
    ): Flow<SyncProgress> = callbackFlow {
        val closed = AtomicBoolean(false)
        val sink = object : SyncProgressSink {
            override fun started(total: UInt) { trySendBlocking(SyncProgress.Started(total.toInt())) }
            override fun item(index: UInt, total: UInt, groupKey: String, displayName: String, phase: SyncPhase) {
                trySendBlocking(SyncProgress.Item(index.toInt(), total.toInt(), groupKey, displayName, phase))
            }
            override fun isCancelled(): Boolean = closed.get() || cancelled()
        }
        launch(io) {
            val summary = runCatchingCancellable { engineExecute(vault, plan, deviceId, dav, saveIo(write), sink) }
                .getOrElse { e ->
                    log.log(Level.WARNING, "execute failed", e)
                    failedSummary(e.message ?: "Sync failed")
                }
            send(SyncProgress.Done(summary))
            close()
        }
        awaitClose { closed.set(true) }
    }.buffer(Channel.UNLIMITED)

    /** Called on the engine thread; a throw reaches Rust as a per-save error, so log the trace here. */
    private fun saveIo(write: suspend (Int, NormalizedSave, List<FileEntry>) -> Boolean) = object : SaveIo {
        override fun restore(index: UInt, save: NormalizedSave, files: List<FileEntry>): Boolean = try {
            runBlocking { write(index.toInt(), save, files) }
                .also { log.info("restore #$index ${save.groupKey} (${files.size} file(s)) -> $it") }
        } catch (e: Exception) {
            log.log(Level.WARNING, "restore #$index ${save.groupKey} failed", e)
            throw e
        }
    }

    /**
     * Safety snapshot FIRST (abort on failure when enabled), then write, then prune.
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
