package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class SnapshotsReady(val snapshots: List<SnapshotInfo>)

class SnapshotsViewModel(
    application: Application,
    private val session: SessionViewModel,
    private val settings: SettingsProvider,
    private val saveSourceProvider: SaveSourceProvider,
    private val snapshotStoreProvider: SnapshotStoreProvider,
    private val groupKey: String,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<SnapshotsReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<SnapshotsReady>> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatching {
                snapshotStoreProvider.listSnapshots(groupKey)
            }.onSuccess { snapshots ->
                Log.i(TAG, "snapshot list complete for $groupKey: ${snapshots.size} snapshots")
                _uiState.value = UiState.Ready(SnapshotsReady(snapshots))
            }.onFailure { e ->
                Log.e(TAG, "snapshot list failed for $groupKey", e)
                _uiState.value = UiState.Error(e.message ?: "Snapshot load failed")
            }
        }
    }

    // Offline restore into the folder the save was scanned from; safety snapshot first.
    fun restore(snapshotId: String) {
        viewModelScope.launch {
            runCatching {
                val loaded = settings.load()
                val (folder, save) = saveSourceProvider.findSave(loaded, groupKey)
                    ?: error("Save no longer available: $groupKey")
                val files = snapshotStoreProvider.readSnapshot(groupKey, snapshotId)
                val repo = session.syncRepo() ?: error("Vault is locked")
                restoreIntoSource(
                    repo = repo,
                    save = save,
                    folder = folder,
                    files = files,
                    saveSourceProvider = saveSourceProvider,
                    snapshotStoreProvider = snapshotStoreProvider,
                    safetyBackup = loaded.safetyBackup,
                )
            }.onSuccess { ok ->
                Log.i(TAG, "restored $groupKey from snapshot $snapshotId (restored=$ok)")
            }.onFailure { e ->
                Log.e(TAG, "snapshot restore failed for $groupKey/$snapshotId", e)
                _uiState.value = UiState.Error(e.message ?: "Restore failed")
            }
        }
    }

    private companion object {
        const val TAG = "WS:SNAPSHOTS"
    }
}
