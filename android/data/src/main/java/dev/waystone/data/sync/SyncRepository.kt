package dev.waystone.data.sync

import java.util.logging.Level
import java.util.logging.Logger
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.DeviceHead
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.HistoryEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PullOutcome
import uniffi.waystone_mobile.PushOutcome
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.WaystoneException
import uniffi.waystone_mobile.decidePull as ffiDecidePull
import uniffi.waystone_mobile.fetchBlob as ffiFetchBlob
import uniffi.waystone_mobile.listHistory as ffiListHistory
import uniffi.waystone_mobile.localHash as ffiLocalHash
import uniffi.waystone_mobile.pullOne as ffiPullOne
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

data class SyncResult(
    val save: NormalizedSave,
    val decision: SyncDecision,
    /** Decrypted remote files when this save was pulled; null for push/no-op decisions. */
    val files: List<FileEntry>?,
)

/** Mirrors desktop/src/tui/action.rs sync flows over the UniFFI binding. */
class SyncRepository(
    private val vault: Vault,
    val dav: WebDav,
    private val deviceId: String,
    private val io: CoroutineContext = Dispatchers.IO,
) {

    private val log = Logger.getLogger(SyncRepository::class.java.name)

    suspend fun hashOf(save: NormalizedSave): String = withContext(io) { ffiLocalHash(save) }

    suspend fun statusOf(save: NormalizedSave): SourceStatus = withContext(io) {
        try {
            val hash = ffiLocalHash(save)
            val heads = ffiReadRemoteHeads(vault, save, dav)
            when (val d = ffiDecidePull(hash, save.mtime, heads, deviceId, ConflictPolicy.PROMPT)) {
                SyncDecision.InSync -> SourceStatus.InSync
                SyncDecision.Push -> SourceStatus.Ahead
                is SyncDecision.Pull -> SourceStatus.Behind
                is SyncDecision.ConflictResolved ->
                    if (d.winner == "remote") SourceStatus.Behind else SourceStatus.Ahead
                is SyncDecision.ConflictNeedsInput -> SourceStatus.Conflict
            }
        } catch (e: Exception) {
            log.log(Level.WARNING, "status scan failed for ${save.groupKey}", e)
            SourceStatus.Error
        }
    }

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

    suspend fun pullOne(save: NormalizedSave, policy: ConflictPolicy): PullOutcome = withContext(io) {
        ffiPullOne(vault, save, deviceId, dav, policy)
            .also { log.info("pull ${save.groupKey} -> ${it.decision}") }
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

    suspend fun syncAll(saves: List<NormalizedSave>, policy: ConflictPolicy): List<SyncResult> {
        val results = mutableListOf<SyncResult>()
        for (save in saves) {
            val decision = withContext(io) {
                val hash = ffiLocalHash(save)
                val heads = ffiReadRemoteHeads(vault, save, dav)
                ffiDecidePull(hash, save.mtime, heads, deviceId, policy)
            }
            val (final, files) = when (decision) {
                SyncDecision.Push -> {
                    pushOne(save)
                    decision to null
                }
                is SyncDecision.Pull, is SyncDecision.ConflictResolved ->
                    pullOne(save, policy).let { it.decision to it.files }
                else -> decision to null
            }
            results.add(SyncResult(save, final, files))
        }
        return results
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
            if (!takeSnapshot(save.groupKey, current)) {
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
