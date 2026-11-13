package dev.waystone.app.vm

import android.app.Application
import dev.waystone.app.log.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.data.sync.SyncRepository
import dev.waystone.data.sync.runCatchingCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.redactUrl

data class SetupReady(val recoveryHex: String, val deviceId: String = "")

class SetupViewModel(
    application: Application,
    private val settings: SettingsProvider,
    sessionVaultStore: SessionVaultStore,
    private val davFactory: DavFactory,
    private val session: SessionViewModel,
) : AndroidViewModel(application) {

    private val engine = SetupEngine(sessionVaultStore)

    private val _uiState = MutableStateFlow<UiState<SetupReady>>(UiState.Ready(SetupReady("")))
    val uiState: StateFlow<UiState<SetupReady>> = _uiState.asStateFlow()

    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

    private val _prefill = MutableStateFlow(SetupPrefill.EMPTY)
    val prefill: StateFlow<SetupPrefill> = _prefill.asStateFlow()

    init {
        viewModelScope.launch {
            runCatchingCancellable { SetupPrefill.of(settings.load()) }
                .onSuccess {
                    Log.i(TAG, "prefill: server=${redactUrl(it.serverUrl)} username=${if (it.username.isEmpty()) "none" else "set"}")
                    _prefill.value = it
                }
                .onFailure { e -> Log.e(TAG, "prefill load failed, starting empty", e) }
        }
    }

    fun submit(
        serverUrl: String,
        username: String,
        password: String,
        encrypt: Boolean,
        passphrase: String,
        confirm: String,
    ) {
        SetupValidator.validateForm(serverUrl, encrypt, passphrase, confirm)?.let {
            _uiState.value = UiState.Error(it)
            return
        }
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            Log.i(TAG, "setup start: server=${redactUrl(serverUrl)} encrypt=$encrypt")
            val outcome = runCatchingCancellable {
                val dav = davFactory.create(serverUrl, username, password)
                dav to engine.run(dav, username, password.toCharArray(), passphrase.takeIf { encrypt })
            }
            val (dav, result) = outcome.getOrElse { e ->
                Log.e(TAG, "setup failed", e)
                _uiState.value = UiState.Error(e.message ?: "Setup failed")
                return@launch
            }
            when (result) {
                is SetupResult.Created -> finish(serverUrl, username, dav, result.vault, result.recoveryHex)
                is SetupResult.JoinedPlain -> finish(serverUrl, username, dav, result.vault, null)
                SetupResult.VaultExists -> {
                    Log.e(TAG, "setup refused: an encrypted vault already exists on the server")
                    _navigateTo.value = "unlock"
                    _uiState.value = UiState.Error("A vault already exists on this server. Use Unlock instead.")
                }
                is SetupResult.Failed -> {
                    Log.e(TAG, "setup failed: ${result.message}")
                    _uiState.value = UiState.Error(result.message)
                }
            }
        }
    }

    private suspend fun finish(serverUrl: String, username: String, dav: WebDav, vault: Vault, recoveryHex: String?) {
        val existing = settings.load()
        settings.save(existing.copy(serverUrl = serverUrl.trim(), username = username))
        session.attach(vault, dav, SyncRepository(vault, dav, existing.deviceId))
        if (recoveryHex == null) {
            Log.i(TAG, "setup complete (plain server), navigating to dashboard")
            _uiState.value = UiState.Ready(SetupReady(""))
            _navigateTo.value = "dashboard"
        } else {
            Log.i(TAG, "setup complete, encrypted vault initialized on server")
            _uiState.value = UiState.Ready(SetupReady(recoveryHex, existing.deviceId))
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

    private companion object {
        const val TAG = "WS:SETUP"
    }
}
