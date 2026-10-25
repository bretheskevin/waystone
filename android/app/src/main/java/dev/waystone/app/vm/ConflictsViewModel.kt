package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.sync.SyncRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.SyncDecision

class ConflictsViewModel(
    application: Application,
    private val session: SessionViewModel,
    private val settings: SettingsProvider,
    private val saveSourceProvider: SaveSourceProvider,
    private val snapshotStoreProvider: SnapshotStoreProvider,
) : AndroidViewModel(application) {

    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    private val inbox = ConflictInbox(
        scope = viewModelScope,
        loadSaves = { saveSourceProvider.scanSources(settings.load()).flatMap { it.saves } },
        scanOne = { save ->
            val repo = requireRepo()
            val planned = repo.planOne(save, ConflictPolicy.PROMPT)
            (planned.decision as? SyncDecision.ConflictNeedsInput)?.let { repo.conflictEntry(save, planned.heads, it) }
        },
        onKeepLocal = { entry -> requireRepo().pushOne(entry.save) },
        onKeepRemote = { entry ->
            val loaded = settings.load()
            val (folder, save) = saveSourceProvider.findSave(loaded, entry.save.groupKey)
                ?: error("Save not found: ${entry.save.groupKey}")
            val repo = requireRepo()
            val ok = restoreIntoSource(
                repo = repo, save = save, folder = folder,
                files = repo.keepRemoteFiles(save, entry.remoteHash),
                saveSourceProvider = saveSourceProvider, snapshotStoreProvider = snapshotStoreProvider,
                safetyBackup = loaded.safetyBackup,
            )
            check(ok) { "restore aborted (safety snapshot failed)" }
        },
    )
    val state: StateFlow<ConflictInboxState> = inbox.state

    init {
        viewModelScope.launch {
            inbox.state.collect { s ->
                if (s.finishedEmpty && s.error == null) {
                    Log.i(TAG, "conflict inbox empty, returning to dashboard")
                    _navigateTo.value = "dashboard"
                }
            }
        }
        refresh()
    }

    fun refresh() = inbox.startScan()
    fun keepLocal(groupKey: String) = inbox.resolve(groupKey, keepRemote = false)
    fun keepRemote(groupKey: String) = inbox.resolve(groupKey, keepRemote = true)
    fun consumeNavigation() { _navigateTo.value = null }

    private fun requireRepo(): SyncRepository = session.syncRepo() ?: error("Vault is locked")

    private companion object { const val TAG = "WS:CONFLICTS" }
}
