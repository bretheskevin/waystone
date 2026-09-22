package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.sync.ConflictEntry
import dev.waystone.data.sync.SyncRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.decidePull
import uniffi.waystone_mobile.localHash
import uniffi.waystone_mobile.readRemoteHeads

data object ConflictsReady

class ConflictsViewModel(
    application: Application,
    private val session: SessionViewModel,
    private val settings: SettingsProvider,
    private val saveSourceProvider: SaveSourceProvider,
    private val snapshotStoreProvider: SnapshotStoreProvider,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<ConflictsReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<ConflictsReady>> = _uiState.asStateFlow()

    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    val conflicts = MutableStateFlow<List<ConflictEntry>>(emptyList())

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatching {
                val repo = session.syncRepo() ?: error("Vault is locked")
                val loaded = settings.load()
                val scans = saveSourceProvider.scanSources(loaded)
                scans.flatMap { scan ->
                    scan.saves.mapNotNull { save ->
                        withContext(Dispatchers.IO) {
                            runCatching {
                                val vault = session.unlockedVault() ?: error("Vault is locked")
                                val heads = readRemoteHeads(vault, save, repo.dav)
                                val decision = decidePull(
                                    localHash(save), save.mtime, heads, loaded.deviceId, ConflictPolicy.PROMPT,
                                )
                                if (decision is SyncDecision.ConflictNeedsInput) {
                                    repo.conflictEntry(save, heads, decision)
                                } else {
                                    null
                                }
                            }.getOrElse { e ->
                                Log.e(TAG, "conflict scan failed for ${save.groupKey}", e)
                                null
                            }
                        }
                    }
                }
            }.onSuccess { entries ->
                conflicts.value = entries
                if (entries.isEmpty()) {
                    Log.i(TAG, "conflict inbox empty, returning to dashboard")
                    _navigateTo.value = "dashboard"
                } else {
                    Log.i(TAG, "conflict scan complete: ${entries.size} conflicts")
                }
                _uiState.value = UiState.Ready(ConflictsReady)
            }.onFailure { e ->
                Log.e(TAG, "conflict scan failed", e)
                _uiState.value = UiState.Error(e.message ?: "Conflict scan failed")
            }
        }
    }

    fun keepLocal(groupKey: String) {
        viewModelScope.launch {
            runCatching {
                val save = findSave(groupKey)
                val repo = session.syncRepo() ?: error("Vault is locked")
                repo.pushOne(save)
            }.onSuccess {
                Log.i(TAG, "kept local for $groupKey")
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "keep local failed for $groupKey", e)
                _uiState.value = UiState.Error(e.message ?: "Keep local failed")
            }
        }
    }

    fun keepRemote(groupKey: String) {
        viewModelScope.launch {
            runCatching {
                val loaded = settings.load()
                val (folder, save) = saveSourceProvider.findSave(loaded, groupKey)
                    ?: error("Save not found: $groupKey")
                val repo = session.syncRepo() ?: error("Vault is locked")
                val entry = conflicts.value.firstOrNull { it.save.groupKey == groupKey }
                    ?: error("Conflict no longer present: $groupKey")
                val files = repo.keepRemoteFiles(save, entry.remoteHash)
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
                Log.i(TAG, "kept remote for $groupKey (restored=$ok)")
                refresh()
            }.onFailure { e ->
                Log.e(TAG, "keep remote failed for $groupKey", e)
                _uiState.value = UiState.Error(e.message ?: "Keep remote failed")
            }
        }
    }

    fun consumeNavigation() {
        _navigateTo.value = null
    }

    private suspend fun findSave(groupKey: String): NormalizedSave {
        val loaded = settings.load()
        return saveSourceProvider.findSave(loaded, groupKey)?.second
            ?: error("Save not found: $groupKey")
    }

    private companion object {
        const val TAG = "WS:CONFLICTS"
    }
}
