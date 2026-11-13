package dev.waystone.app.vm

import android.app.Application
import android.content.Intent
import dev.waystone.app.log.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.app.log.LogShare
import dev.waystone.data.config.Settings
import dev.waystone.data.sync.runCatchingCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import uniffi.waystone_mobile.redactUrl

data class SettingsReady(
    val serverUrl: String,
    val username: String,
    val deviceId: String,
    val conflictPolicy: String,
    val safetyBackup: Boolean,
    val appVersion: String,
)

class SettingsViewModel(
    application: Application,
    private val settings: SettingsProvider,
    private val sessionVaultStore: SessionVaultStore,
    private val session: SessionViewModel,
    private val appVersion: String,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<SettingsReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<SettingsReady>> = _uiState.asStateFlow()

    // One-shot navigation events (log out / server change → unlock or setup).
    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    private val _shareLogsIntent = MutableStateFlow<Intent?>(null)
    val shareLogsIntent: StateFlow<Intent?> = _shareLogsIntent.asStateFlow()
    private val _logsStatus = MutableStateFlow<String?>(null)
    val logsStatus: StateFlow<String?> = _logsStatus.asStateFlow()

    init {
        load()
    }

    fun shareLogs() {
        val app = getApplication<Application>()
        val files = LogShare.filesToShare(LogShare.logDir(app))
        Log.i(TAG, "share logs (${files.size} file(s))")
        val intent = LogShare.intent(app, files)
        if (intent == null) {
            _logsStatus.value = "No logs yet"
        } else {
            _logsStatus.value = null
            _shareLogsIntent.value = intent
        }
    }

    fun consumeShareLogs() {
        _shareLogsIntent.value = null
    }

    fun load() {
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            publish("load", runCatching { settings.load().toReady() })
        }
    }

    fun setConflictPolicy(policy: String) = update { it.copy(conflictPolicy = policy) }

    fun setSafetyBackup(enabled: Boolean) = update { it.copy(safetyBackup = enabled) }

    // Changing the server invalidates the wrapped MDK and stored WebDAV creds.
    fun changeServer(serverUrl: String, username: String, password: String) {
        viewModelScope.launch {
            runCatchingCancellable {
                val old = settings.load()
                val new = old.copy(serverUrl = serverUrl.trim(), username = username)
                if (dev.waystone.data.config.SettingsStore.serverChanged(old, new)) {
                    Log.i(TAG, "server changed: ${redactUrl(old.serverUrl)} -> ${redactUrl(new.serverUrl)}")
                    forgetSession(sessionVaultStore, session::clear) { Log.i(TAG, it) }
                }
                settings.save(new)
                new.toReady()
            }.onSuccess { ready ->
                _uiState.value = UiState.Ready(ready)
                if (password.isNotEmpty()) {
                    sessionVaultStore.saveCreds(username, password.toCharArray())
                }
                _navigateTo.value = "unlock"
            }.onFailure { e ->
                Log.e(TAG, "server change failed", e)
                _uiState.value = UiState.Error(e.message ?: "Server change failed")
            }
        }
    }

    fun logout() {
        viewModelScope.launch {
            Log.i(TAG, "logout requested")
            runCatchingCancellable {
                forgetSession(sessionVaultStore, session::clear) { Log.i(TAG, it) }
            }.onSuccess {
                Log.i(TAG, "logged out, session credentials cleared")
                _navigateTo.value = "unlock"
            }.onFailure { e ->
                Log.e(TAG, "logout failed", e)
                _uiState.value = UiState.Error(e.message ?: "Logout failed")
            }
        }
    }

    fun consumeNavigation() {
        _navigateTo.value = null
    }

    private fun update(transform: (Settings) -> Settings) {
        viewModelScope.launch {
            val result = runCatching {
                val updated = transform(settings.load())
                settings.save(updated)
                updated.toReady()
            }
            publish("update", result)
        }
    }

    private fun publish(action: String, result: Result<SettingsReady>) {
        result.onSuccess {
            Log.i(TAG, "settings $action OK (app v$appVersion)")
            _uiState.value = UiState.Ready(it)
        }.onFailure { e ->
            Log.e(TAG, "settings $action failed", e)
            _uiState.value = UiState.Error(e.message ?: "Settings $action failed")
        }
    }

    private fun Settings.toReady() = SettingsReady(
        serverUrl = serverUrl,
        username = username.orEmpty(),
        deviceId = deviceId,
        conflictPolicy = conflictPolicy,
        safetyBackup = safetyBackup,
        appVersion = appVersion,
    )

    private companion object {
        const val TAG = "WS:SETTINGS"
    }
}
