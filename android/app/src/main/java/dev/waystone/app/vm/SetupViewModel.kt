package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.sync.SyncRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.vaultInit

data class SetupReady(val recoveryHex: String)

class SetupViewModel(
    application: Application,
    private val settings: SettingsProvider,
    private val sessionVaultStore: SessionVaultStore,
    private val davFactory: DavFactory,
    private val session: SessionViewModel,
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<UiState<SetupReady>>(UiState.Ready(SetupReady("")))
    val uiState: StateFlow<UiState<SetupReady>> = _uiState.asStateFlow()

    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    fun submit(serverUrl: String, username: String, password: String, passphrase: String, confirm: String) {
        SetupValidator.validateForm(serverUrl, passphrase, confirm)?.let {
            _uiState.value = UiState.Error(it)
            return
        }
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            runCatching {
                val dav = davFactory.create(serverUrl, username, password)
                // OkHttpWebDav calls are blocking; keep every DAV touch off the main thread.
                val init = withContext(Dispatchers.IO) {
                    if (dav.get("/keys.json") != null) {
                        throw VaultExistsException()
                    }
                    dav.mkdirP("/")
                    val init = vaultInit(passphrase)
                    dav.put("/keys.json", init.keysJson)
                    init
                }
                if (!sessionVaultStore.wrapMdk(init.vault.exportMdk())) {
                    error("Failed to store the session key")
                }
                sessionVaultStore.saveCreds(username, password.toCharArray())
                val existing = settings.load()
                settings.save(existing.copy(serverUrl = serverUrl.trim(), username = username))
                session.attach(init.vault, dav, SyncRepository(init.vault, dav, existing.deviceId))
                init
            }.onSuccess { init ->
                Log.i(TAG, "setup complete, vault initialized on server")
                _uiState.value = UiState.Ready(SetupReady(init.recoveryHex))
            }.onFailure { e ->
                Log.e(TAG, "setup failed", e)
                if (e is VaultExistsException) {
                    _navigateTo.value = "unlock"
                }
                _uiState.value = UiState.Error(
                    if (e is VaultExistsException) {
                        "A vault already exists on this server. Use Unlock instead."
                    } else {
                        e.message ?: "Setup failed"
                    },
                )
            }
        }
    }

    fun acknowledgeRecovery() {
        // Strings are immutable; dropping the reference is the best we can do on the JVM.
        _uiState.value = UiState.Ready(SetupReady(""))
        _navigateTo.value = "dashboard"
        Log.i(TAG, "recovery key acknowledged, navigating to dashboard")
    }

    fun consumeNavigation() {
        _navigateTo.value = null
    }

    private class VaultExistsException : Exception()

    private companion object {
        const val TAG = "WS:SETUP"
    }
}
