package dev.waystone.app.vm

import android.app.Application
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.app.update.ApkInstaller
import dev.waystone.app.update.InstallEvents
import dev.waystone.data.config.Settings
import dev.waystone.data.update.Updater
import java.io.File
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

    private val releases = Updater()

    val updater = UpdateController(
        scope = viewModelScope,
        currentVersion = appVersion,
        check = { current -> releases.checkLatest(current) },
        download = { url, onProgress, isCancelled ->
            val dest = File(getApplication<Application>().cacheDir, "update.apk")
            dest.takeIf { releases.download(url, it, onProgress, isCancelled) }
        },
        canInstall = { getApplication<Application>().packageManager.canRequestPackageInstalls() },
        install = { apk -> ApkInstaller.install(getApplication(), apk) },
    )

    init {
        load()
        viewModelScope.launch {
            InstallEvents.results.collect { r ->
                if (r != null) {
                    updater.onInstallResult(r.success, r.message)
                    InstallEvents.results.value = null
                }
            }
        }
    }

    fun openInstallPermissionSettings() {
        val app = getApplication<Application>()
        Log.i("WS:UPDATE", "opening unknown-sources settings for ${app.packageName}")
        app.startActivity(
            Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
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
