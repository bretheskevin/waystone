package dev.waystone.app.vm

import dev.waystone.data.config.Settings
import dev.waystone.data.config.SourceFolder
import dev.waystone.data.sync.SyncRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.WebDav

// Bound to :data impls in ServiceLocator. Kept as interfaces so screens/VMs never
// construct Android singletons themselves.

interface SettingsProvider {
    suspend fun load(): Settings
    suspend fun save(settings: Settings)
}

interface SessionVaultStore {
    suspend fun unwrapMdk(): ByteArray?
    suspend fun wrapMdk(mdk: ByteArray): Boolean
    suspend fun loadCreds(): Pair<String, CharArray>?
    suspend fun saveCreds(username: String, password: CharArray): Boolean
    suspend fun clear()
}

interface DavFactory {
    suspend fun create(serverUrl: String, username: String, password: String): WebDav
}

interface DavProvider {
    /** Null when server or stored credentials are missing. */
    suspend fun current(): WebDav?
}

data class SourceScan(
    val folder: SourceFolder,
    val saves: List<NormalizedSave>,
    val error: String? = null,
)

interface SaveSourceProvider {
    suspend fun scanSources(settings: Settings): List<SourceScan>
    suspend fun findSave(settings: Settings, groupKey: String): Pair<SourceFolder, NormalizedSave>?

    // Blocking SAF write; callers invoke on an IO dispatcher (guardedRestore's write lambda is not suspend).
    // `save` lets ROM-keyed sources map canonical files to the local ROM's native save names.
    fun writeFiles(folder: SourceFolder, save: NormalizedSave, files: List<FileEntry>): Int
}

data class SnapshotInfo(
    val id: String,
    val groupKey: String,
    val timestamp: String,
)

interface SnapshotStoreProvider {
    suspend fun listSnapshots(groupKey: String): List<SnapshotInfo>
    suspend fun readSnapshot(groupKey: String, snapshotId: String): List<FileEntry>

    /** Safety snapshot of the current local files; false → callers must abort the restore. */
    suspend fun snapshot(groupKey: String, files: Map<String, ByteArray>): Boolean

    suspend fun prune(groupKey: String)
}

/**
 * Shared guarded-restore wiring for app-layer callers (conflicts/history/snapshots/dashboard):
 * SAF write + snapshot seam + prune, off the main thread. Callers decide which files count as
 * "current local" — a freshly scanned save is preferred over a stale one.
 */
internal suspend fun restoreIntoSource(
    repo: SyncRepository,
    save: NormalizedSave,
    folder: SourceFolder,
    files: List<FileEntry>,
    saveSourceProvider: SaveSourceProvider,
    snapshotStoreProvider: SnapshotStoreProvider,
    safetyBackup: Boolean,
    currentLocalFiles: () -> List<FileEntry> = { save.files },
): Boolean = withContext(Dispatchers.IO) {
    repo.guardedRestore(
        save = save,
        files = files,
        write = { saveSourceProvider.writeFiles(folder, save, it) },
        currentLocalFiles = currentLocalFiles,
        takeSnapshot = { key, map -> snapshotStoreProvider.snapshot(key, map) },
        prune = { snapshotStoreProvider.prune(it) },
        safetyBackup = safetyBackup,
    )
}
