package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.config.Settings
import dev.waystone.data.config.SourceFolder
import dev.waystone.data.sync.SyncRepository
import dev.waystone.data.sync.SourceStatus
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave

data class SaveRow(
    val groupKey: String,
    val displayName: String,
    val mtime: String,
    val status: String,
)

data class SourceCard(
    val folder: SourceFolder,
    val saves: List<SaveRow>,
    val error: String? = null,
) {
    val worstStatus: String
        get() = SourceStatus.worst(saves.mapNotNull { row ->
            runCatching { SourceStatus.valueOf(row.status) }.getOrNull()
        }).name
}

data class DashboardReady(
    val sources: List<SourceCard>,
    val conflictCount: Int,
)

class DashboardViewModel(
    application: Application,
    private val session: SessionViewModel,
    private val settings: SettingsProvider,
    private val saveSourceProvider: SaveSourceProvider,
    private val snapshotStoreProvider: SnapshotStoreProvider,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<DashboardReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<DashboardReady>> = _uiState.asStateFlow()

    private var loadedSettings: Settings? = null
    private var lastScan: Map<String, Pair<SourceFolder, NormalizedSave>> = emptyMap()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatching {
                val repo = requireRepo()
                val loaded = settings.load()
                loadedSettings = loaded
                val scans = saveSourceProvider.scanSources(loaded)
                val cards = scans.map { scan ->
                    val rows = scan.saves.map { save ->
                        val status = repo.statusOf(save)
                        SaveRow(save.groupKey, save.displayName, save.mtime, status.name)
                    }
                    if (scan.saves.isEmpty() && scan.error == null) {
                        Log.i(TAG, "source ${scan.folder.uri} holds no saves")
                    }
                    SourceCard(scan.folder, rows, scan.error)
                }
                lastScan = scans.flatMap { scan ->
                    scan.saves.map { it.groupKey to (scan.folder to it) }
                }.toMap()
                DashboardReady(cards, cards.sumOf { card -> card.saves.count { it.status == SourceStatus.Conflict.name } })
            }.onSuccess { ready ->
                Log.i(TAG, "status scan complete: ${ready.sources.size} sources, ${ready.conflictCount} conflicts")
                _uiState.value = UiState.Ready(ready)
            }.onFailure { e ->
                Log.e(TAG, "status scan failed", e)
                _uiState.value = UiState.Error(e.message ?: "Status scan failed")
            }
        }
    }

    fun push(groupKey: String) = perSave(groupKey, "push") { repo, save -> repo.pushOne(save) }

    fun pull(groupKey: String) = perSave(groupKey, "pull") { repo, save ->
        val outcome = repo.pullOne(save, policyOf())
        // pullOne returns decrypted remote files; write them via SAF (guarded), like desktop pull_target.
        val files = outcome.files ?: return@perSave
        val folder = lastScan[save.groupKey]?.first
            ?: error("Source folder not found: ${save.groupKey}")
        val loaded = loadedSettings ?: settings.load()
        val current = saveSourceProvider.findSave(loaded, save.groupKey)?.second?.files ?: save.files
        restoreFiles(save, files, folder, current)
    }

    fun syncSource(folder: SourceFolder) = syncBatch(
        logMsg = "sync source ${folder.uri}",
        uiError = "Source sync failed",
    ) { scans -> scans.filter { it.folder == folder }.flatMap { it.saves } }

    fun syncAll() = syncBatch(logMsg = "sync all", uiError = "Sync failed") { scans ->
        scans.flatMap { it.saves }
    }

    fun addSource(uri: String, adapter: String, system: String) {
        viewModelScope.launch {
            runCatching {
                val loaded = loadedSettings ?: settings.load()
                val updated = loaded.copy(sources = loaded.sources + SourceFolder(uri, adapter, system))
                settings.save(updated)
                loadedSettings = updated
            }.onSuccess {
                Log.i(TAG, "source added: $uri ($adapter/$system)")
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "add source failed for $uri", e)
                _uiState.value = UiState.Error(e.message ?: "Add source failed")
            }
        }
    }

    private fun perSave(groupKey: String, action: String, block: suspend (SyncRepository, NormalizedSave) -> Unit) {
        viewModelScope.launch {
            runCatching {
                val save = lastScan[groupKey]?.second ?: error("Save not found: $groupKey")
                block(requireRepo(), save)
            }.onSuccess {
                Log.i(TAG, "$action OK: $groupKey")
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "$action failed: $groupKey", e)
                _uiState.value = UiState.Error(e.message ?: "$action failed")
            }
        }
    }

    /**
     * Write pulled/remote files into the save's source folder, safety snapshot first.
     * The caller supplies the files that count as "current local" — a freshly scanned
     * save (mirroring desktop guarded_restore), not a stale cached one.
     */
    private suspend fun restoreFiles(
        save: NormalizedSave,
        files: List<FileEntry>,
        folder: SourceFolder,
        currentFiles: List<FileEntry>,
    ) {
        val loaded = loadedSettings ?: settings.load()
        restoreIntoSource(
            repo = requireRepo(),
            save = save,
            folder = folder,
            files = files,
            saveSourceProvider = saveSourceProvider,
            snapshotStoreProvider = snapshotStoreProvider,
            safetyBackup = loaded.safetyBackup,
            currentLocalFiles = { currentFiles },
        )
    }

    /**
     * One fresh scan per batch drives both the sync decisions and the safety snapshots,
     * so a many-save pull does not re-scan every source folder per save.
     */
    private fun syncBatch(
        logMsg: String,
        uiError: String,
        select: (List<SourceScan>) -> List<NormalizedSave>,
    ) {
        viewModelScope.launch {
            runCatching {
                val repo = requireRepo()
                val scans = saveSourceProvider.scanSources(settings.load())
                val currentByKey = scans.flatMap { scan ->
                    scan.saves.map { it.groupKey to (scan.folder to it.files) }
                }.toMap()
                val selected = select(scans)
                Log.i(TAG, "$logMsg: ${selected.size} saves")
                repo.syncAll(selected, policyOf()) to currentByKey
            }.onSuccess { (results, currentByKey) ->
                results.forEach { r ->
                    r.files?.let { files ->
                        val entry = currentByKey[r.save.groupKey]
                        if (entry == null) {
                            Log.w(TAG, "skip restore for ${r.save.groupKey}: source save vanished mid-sync")
                        } else {
                            restoreFiles(r.save, files, entry.first, entry.second)
                        }
                    }
                }
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "$logMsg failed", e)
                _uiState.value = UiState.Error(e.message ?: uiError)
            }
        }
    }

    private suspend fun policyOf(): ConflictPolicy =
        if (settings.load().conflictPolicy == "prompt") ConflictPolicy.PROMPT else ConflictPolicy.NEWEST_WINS

    private fun requireRepo(): SyncRepository =
        session.syncRepo() ?: error("Vault is locked")

    private companion object {
        const val TAG = "WS:DASHBOARD"
    }
}
