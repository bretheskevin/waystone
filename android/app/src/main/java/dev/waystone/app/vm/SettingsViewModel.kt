package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.config.Settings
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class SettingsReady(
    val serverUrl: String,
    val username: String,
    val deviceId: String,
    val conflictPolicy: String,
    val safetyBackup: Boolean,
)

class SettingsViewModel(
    application: Application,
    private val settings: SettingsProvider,
    private val sessionVaultStore: SessionVaultStore,
    private val session: SessionViewModel,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<SettingsReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<SettingsReady>> = _uiState.asStateFlow()

    // One-shot navigation events (log out / server change → unlock or setup).
    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    init {
        load()
    }

    fun load() {
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatching { settings.load().toReady() }
                .onSuccess { _uiState.value = UiState.Ready(it) }
                .onFailure { e ->
                    Log.e(TAG, "settings load failed", e)
                    _uiState.value = UiState.Error(e.message ?: "Settings load failed")
                }
        }
    }

    fun setConflictPolicy(policy: String) = update { it.copy(conflictPolicy = policy) }

    fun setSafetyBackup(enabled: Boolean) = update { it.copy(safetyBackup = enabled) }

    // Changing the server invalidates the wrapped MDK and stored WebDAV creds.
    fun changeServer(serverUrl: String, username: String, password: String) {
        viewModelScope.launch {
            runCatching {
                val old = settings.load()
                val new = old.copy(serverUrl = serverUrl.trim(), username = username)
                if (dev.waystone.data.config.SettingsStore.serverChanged(old, new)) {
                    sessionVaultStore.clear()
                    session.clear()
                    Log.i(TAG, "server changed: ${old.serverUrl} -> ${new.serverUrl}; session credentials cleared")
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
            runCatching {
                sessionVaultStore.clear()
                session.clear()
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
            runCatching {
                val updated = transform(settings.load())
                settings.save(updated)
                updated.toReady()
            }.onSuccess { _uiState.value = UiState.Ready(it) }
                .onFailure { e ->
                    Log.e(TAG, "settings update failed", e)
                    _uiState.value = UiState.Error(e.message ?: "Settings update failed")
                }
        }
    }

    private fun Settings.toReady() = SettingsReady(
        serverUrl = serverUrl,
        username = username.orEmpty(),
        deviceId = deviceId,
        conflictPolicy = conflictPolicy,
        safetyBackup = safetyBackup,
    )

    private companion object {
        const val TAG = "WS:SETTINGS"
    }
}
