package dev.waystone.app.vm

import android.app.Application
import dev.waystone.app.log.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import androidx.compose.ui.graphics.ImageBitmap
import dev.waystone.app.nav.Routes
import dev.waystone.app.ui.toImageBitmap
import dev.waystone.data.config.ROM_KEYED_ADAPTER
import java.util.Optional
import java.util.concurrent.ConcurrentHashMap
import dev.waystone.data.config.Settings
import dev.waystone.data.config.SourceFolder
import dev.waystone.data.sync.SourceStatus
import dev.waystone.data.sync.SyncProgress
import dev.waystone.data.sync.SyncRepository
import dev.waystone.data.sync.failedSummary
import dev.waystone.data.sync.headline
import dev.waystone.data.sync.status
import dev.waystone.data.sync.isRemoteOnly
import dev.waystone.data.sync.runCatchingCancellable
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Dispatchers
import dev.waystone.data.net.NetStatusText
import dev.waystone.data.net.NoInternetReason
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PlanAction
import uniffi.waystone_mobile.SyncPlanEntry
import uniffi.waystone_mobile.engineForcedPush

data class SaveRow(
    val groupKey: String,
    val displayName: String,
    val mtime: String,
    val status: String,
    val remoteOnly: Boolean = false,
    val system: String? = null,
    val artKey: String? = null,
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
    private val networkMonitor: NetworkMonitor,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<DashboardReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<DashboardReady>> = _uiState.asStateFlow()

    private val _syncRun = MutableStateFlow<SyncRunState?>(null)
    val syncRun: StateFlow<SyncRunState?> = _syncRun.asStateFlow()
    private val cancelRequested = AtomicBoolean(false)

    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    private val artCache = ConcurrentHashMap<String, Optional<ImageBitmap>>()

    /** Decoded once per ROM off the main thread (null = no art or load failed, cached too). */
    suspend fun artFor(folder: SourceFolder, artKey: String): ImageBitmap? = withContext(Dispatchers.IO) {
        artCache.getOrPut("${folder.key}|$artKey") {
            Optional.ofNullable(
                runCatchingCancellable { saveSourceProvider.romArt(folder, artKey)?.toImageBitmap() }
                    .onFailure { Log.w(TAG, "art $artKey failed; using the fallback tile", it) }
                    .getOrNull(),
            )
        }.orElse(null)
    }

    fun consumeNavigation() {
        _navigateTo.value = null
    }

    /** Offline: leave a one-shot nav event to the no-internet screen (Retry pops back). */
    private fun offline(): Boolean {
        if (networkMonitor.isOnline()) return false
        Log.i(TAG, "offline -> no-internet screen")
        _navigateTo.value = Routes.NO_INTERNET
        return true
    }

    private var loadedSettings: Settings? = null
    private var refreshSkippedOffline = false

    /** On re-entering the dashboard (e.g. back from the no-internet screen): redo a refresh skipped offline. */
    fun refreshIfSkippedOffline() {
        if (!refreshSkippedOffline || !networkMonitor.isOnline()) return
        Log.i(TAG, "back online: redoing the refresh skipped while offline")
        refresh()
    }

    init {
        refresh()
    }

    fun refresh() {
        if (offline()) {
            refreshSkippedOffline = true
            if (_uiState.value is UiState.Loading) {
                _uiState.value = UiState.Error(NetStatusText.headline(NoInternetReason.NoNetwork))
            }
            return
        }
        refreshSkippedOffline = false
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatchingCancellable {
                val repo = requireRepo()
                val loaded = settings.load()
                loadedSettings = loaded
                val scans = saveSourceProvider.scanSources(loaded, repo.remoteOnlyProbe())
                val cards = scans.map { scan ->
                    val plan = repo.plan(scan.saves, ConflictPolicy.PROMPT)
                    val rows = plan.map { e ->
                        val isRom = e.save.source == ROM_KEYED_ADAPTER
                        SaveRow(
                            e.save.groupKey, e.save.displayName, e.save.mtime, e.status.name, e.save.isRemoteOnly,
                            system = e.save.system.takeIf { isRom },
                            artKey = "${e.save.system}/${e.save.gameKey}".takeIf { isRom },
                        )
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
        val (save, folder) = findIn(scans, groupKey)
        val entry = withContext(Dispatchers.Default) { engineForcedPush(save) }
        if (entry == null) {
            Log.w(TAG, "push refused for remote-only $groupKey")
            error("Nothing local to upload")
        }
        Planned(listOf(entry), listOf(folder))
    }

    fun pull(groupKey: String) = runSync("pull $groupKey") { repo, scans ->
        val (save, folder) = findIn(scans, groupKey)
        planFor(repo, listOf(save to folder)).onlyPulls()
    }

    fun syncSource(folder: SourceFolder) = runSync("sync source ${folder.uri}") { repo, scans ->
        planFor(repo, scans.filter { it.folder == folder }.flatMap { sc -> sc.saves.map { it to sc.folder } })
    }

    fun syncAll() = runSync("sync all") { repo, scans ->
        planFor(repo, scans.flatMap { sc -> sc.saves.map { it to sc.folder } })
    }

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

    private fun findIn(scans: List<SourceScan>, groupKey: String): Pair<NormalizedSave, SourceFolder> =
        scans.firstNotNullOfOrNull { sc -> sc.saves.firstOrNull { it.groupKey == groupKey }?.let { it to sc.folder } }
            ?: error("Save not found: $groupKey")

    /** Plan entries index-aligned with the source folder each save was scanned from (restore targets). */
    private class Planned(val entries: List<SyncPlanEntry>, val folders: List<SourceFolder>) {
        fun onlyPulls(): Planned {
            val keep = entries.indices.filter { entries[it].action == PlanAction.PULL }
            return Planned(keep.map { entries[it] }, keep.map { folders[it] })
        }
    }

    private suspend fun planFor(repo: SyncRepository, pairs: List<Pair<NormalizedSave, SourceFolder>>): Planned =
        Planned(repo.plan(pairs.map { it.first }, policyOf()), pairs.map { it.second })

    /**
     * Write pulled/remote files into the save's source folder, safety snapshot first.
     * [save] must be freshly scanned: its files count as "current local", not a stale cached copy.
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
    private fun runSync(label: String, buildPlan: suspend (SyncRepository, List<SourceScan>) -> Planned) {
        if (_syncRun.value?.running == true) {
            Log.w(TAG, "$label ignored: a sync is already running")
            return
        }
        if (offline()) return
        cancelRequested.set(false)
        _syncRun.value = SyncRunState()
        viewModelScope.launch {
            runCatchingCancellable {
                val repo = requireRepo()
                val scans = saveSourceProvider.scanSources(settings.load(), repo.remoteOnlyProbe())
                val planned = buildPlan(repo, scans)
                Log.i(TAG, "$label: ${planned.entries.size} entries")
                repo.execute(
                    planned.entries,
                    write = { i, save, files -> restoreFiles(save, files, planned.folders[i]) },
                    cancelled = cancelRequested::get,
                ).collect { p ->
                    _syncRun.value = (_syncRun.value ?: SyncRunState()).reduce(p)
                    if (p is SyncProgress.Done) Log.i(TAG, "$label: ${p.summary.headline()}")
                }
            }.onFailure { e ->
                Log.e(TAG, "$label failed", e)
                _syncRun.value = SyncRunState(summary = failedSummary(e.message ?: "Sync failed"))
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
