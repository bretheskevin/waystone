package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class HistoryItem(
    val timestamp: String,
    val deviceId: String,
    val hash: String,
    val mtime: String,
)

data class HistoryReady(val entries: List<HistoryItem>)

class HistoryViewModel(
    application: Application,
    private val session: SessionViewModel,
    private val settings: SettingsProvider,
    private val saveSourceProvider: SaveSourceProvider,
    private val snapshotStoreProvider: SnapshotStoreProvider,
    private val groupKey: String,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<HistoryReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<HistoryReady>> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatching {
                val loaded = settings.load()
                val repo = session.syncRepo() ?: error("Vault is locked")
                val save = saveSourceProvider.findSave(loaded, groupKey)?.second
                    ?: error("Save not found: $groupKey")
                repo.history(save).map { HistoryItem(it.timestamp, it.deviceId, it.hash, it.mtime) }
            }.onSuccess { entries ->
                Log.i(TAG, "history load complete for $groupKey: ${entries.size} entries")
                _uiState.value = UiState.Ready(HistoryReady(entries))
            }.onFailure { e ->
                Log.e(TAG, "history load failed for $groupKey", e)
                _uiState.value = UiState.Error(e.message ?: "History load failed")
            }
        }
    }

    // Force-pull by hash: fetch the historical blob (already decrypted) and restore it locally.
    fun restore(hash: String) {
        viewModelScope.launch {
            runCatching {
                val loaded = settings.load()
                val (folder, save) = saveSourceProvider.findSave(loaded, groupKey)
                    ?: error("Save not found: $groupKey")
                val repo = session.syncRepo() ?: error("Vault is locked")
                val files = repo.keepRemoteFiles(save, hash)
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
                Log.i(TAG, "restored $groupKey @ ${hash.take(12)} (restored=$ok)")
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "restore failed for $groupKey @ ${hash.take(12)}", e)
                _uiState.value = UiState.Error(e.message ?: "Restore failed")
            }
        }
    }

    private companion object {
        const val TAG = "WS:HISTORY"
    }
}
