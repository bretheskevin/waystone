package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.config.Settings
import dev.waystone.data.config.SourceFolder
import dev.waystone.data.sync.SourceStatus
import dev.waystone.data.sync.SyncPlan
import dev.waystone.data.sync.SyncPlanEntry
import dev.waystone.data.sync.SyncProgress
import dev.waystone.data.sync.SyncRepository
import dev.waystone.data.sync.SyncSummary
import dev.waystone.data.sync.isRemoteOnly
import dev.waystone.data.sync.runCatchingCancellable
import java.util.IdentityHashMap
import java.util.concurrent.atomic.AtomicBoolean
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
    val remoteOnly: Boolean = false,
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

    private val _syncRun = MutableStateFlow<SyncRunState?>(null)
    val syncRun: StateFlow<SyncRunState?> = _syncRun.asStateFlow()
    private val cancelRequested = AtomicBoolean(false)

    private var loadedSettings: Settings? = null

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
                val scans = saveSourceProvider.scanSources(loaded, repo.remoteOnlyProbe())
                val cards = scans.map { scan ->
                    val plan = repo.plan(scan.saves, ConflictPolicy.PROMPT)
                    val rows = plan.entries.map { e ->
                        SaveRow(e.save.groupKey, e.save.displayName, e.save.mtime, e.status.name, e.save.isRemoteOnly)
                    }
                    if (scan.saves.isEmpty() && scan.error == null) {
                        Log.i(TAG, "source ${scan.folder.uri} holds no saves")
                    }
                    SourceCard(scan.folder, rows, scan.error)
                }
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

    fun push(groupKey: String) = runSync("push $groupKey") { _, scans ->
        SyncPlan(listOf(SyncPlanEntry.forcedPush(findIn(scans, groupKey))))
    }

    fun pull(groupKey: String) = runSync("pull $groupKey") { repo, scans ->
        repo.plan(listOf(findIn(scans, groupKey)), policyOf()).onlyPulls()
    }

    fun syncSource(folder: SourceFolder) = runSync("sync source ${folder.uri}") { repo, scans ->
        repo.plan(scans.filter { it.folder == folder }.flatMap { it.saves }, policyOf())
    }

    fun syncAll() = runSync("sync all") { repo, scans -> repo.plan(scans.flatMap { it.saves }, policyOf()) }

    fun cancelSync() {
        if (_syncRun.value?.running == true) {
            cancelRequested.set(true)
            _syncRun.value = _syncRun.value?.copy(cancelling = true)
            Log.i(TAG, "sync cancel requested (stops after the current save)")
        }
    }

    fun dismissSync() {
        Log.i(TAG, "sync summary dismissed")
        _syncRun.value = null
        refresh()
    }

    fun addSource(folder: SourceFolder) {
        viewModelScope.launch {
            runCatching {
                val loaded = loadedSettings ?: settings.load()
                val updated = loaded.copy(sources = loaded.sources + folder)
                settings.save(updated)
                loadedSettings = updated
            }.onSuccess {
                Log.i(TAG, "source added: ${folder.uri} (${folder.adapter}, preset=${folder.preset}, saves=${folder.saveUri})")
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "add source failed for ${folder.uri}", e)
                _uiState.value = UiState.Error(e.message ?: "Add source failed")
            }
        }
    }

    private fun findIn(scans: List<SourceScan>, groupKey: String): NormalizedSave =
        scans.firstNotNullOfOrNull { sc -> sc.saves.firstOrNull { it.groupKey == groupKey } }
            ?: error("Save not found: $groupKey")

    /**
     * Write pulled/remote files into the save's source folder, safety snapshot first.
     * [save] must be freshly scanned: its files count as "current local" (mirroring desktop
     * guarded_restore), not a stale cached copy.
     */
    private suspend fun restoreFiles(
        save: NormalizedSave,
        files: List<FileEntry>,
        folder: SourceFolder,
    ): Boolean {
        val loaded = loadedSettings ?: settings.load()
        return restoreIntoSource(
            repo = requireRepo(),
            save = save,
            folder = folder,
            files = files,
            saveSourceProvider = saveSourceProvider,
            snapshotStoreProvider = snapshotStoreProvider,
            safetyBackup = loaded.safetyBackup,
        )
    }

    /** Single sync path (decide-first): one fresh scan feeds the plan and the restore targets. */
    private fun runSync(label: String, buildPlan: suspend (SyncRepository, List<SourceScan>) -> SyncPlan) {
        if (_syncRun.value?.running == true) {
            Log.w(TAG, "$label ignored: a sync is already running")
            return
        }
        cancelRequested.set(false)
        _syncRun.value = SyncRunState()
        viewModelScope.launch {
            runCatchingCancellable {
                val repo = requireRepo()
                val scans = saveSourceProvider.scanSources(settings.load(), repo.remoteOnlyProbe())
                // By identity: the same groupKey can live in two sources (e.g. one ROM folder under two presets).
                val folderOf = IdentityHashMap<NormalizedSave, SourceFolder>().apply {
                    scans.forEach { sc -> sc.saves.forEach { put(it, sc.folder) } }
                }
                val plan = buildPlan(repo, scans)
                Log.i(TAG, "$label: ${plan.entries.size} entries (${plan.describe()})")
                repo.execute(
                    plan,
                    restore = { entry, files ->
                        val folder = folderOf[entry.save]
                            ?: error("no source folder for ${entry.save.groupKey}")
                        restoreFiles(entry.save, files, folder)
                    },
                    isCancelled = cancelRequested::get,
                ).collect { p ->
                    _syncRun.value = (_syncRun.value ?: SyncRunState()).reduce(p)
                    if (p is SyncProgress.Done) Log.i(TAG, "$label: ${p.summary.headline()}")
                }
            }.onFailure { e ->
                Log.e(TAG, "$label failed", e)
                _syncRun.value = SyncRunState(summary = SyncSummary(errors = 1, errorMessages = listOf(e.message ?: "Sync failed")))
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
