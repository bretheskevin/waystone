package dev.waystone.app.vm

import android.app.Application
import dev.waystone.app.log.Log
import dev.waystone.app.nav.NavEvent
import dev.waystone.app.nav.Routes
import dev.waystone.data.sync.runCatchingCancellable
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data object UnlockReady

class UnlockViewModel(
    application: Application,
    settings: SettingsProvider,
    private val sessionVaultStore: SessionVaultStore,
    davProvider: DavProvider,
    davFactory: DavFactory,
    private val session: SessionViewModel,
) : AndroidViewModel(application) {

    private val engine = UnlockEngine(settings, sessionVaultStore, davProvider, davFactory)

    private val _uiState = MutableStateFlow<UiState<UnlockReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<UnlockReady>> = _uiState.asStateFlow()

    private val _navigateTo = MutableStateFlow<NavEvent?>(null)
    val navigateTo: StateFlow<NavEvent?> = _navigateTo.asStateFlow()

    val passphrase = MutableStateFlow("")
    val useRecoveryKey = MutableStateFlow(false)
    val needsWebdavPassword: StateFlow<Boolean> = engine.needsWebdavPassword

    init {
        attemptAutoUnlock()
    }

    fun updateWebdavPassword(value: String) = engine.updateWebdavPassword(value)

    private fun attemptAutoUnlock() {
        viewModelScope.launch {
            when (val result = engine.attemptAutoUnlock()) {
                is UnlockResult.Unlocked -> {
                    passphrase.value = ""
                    session.attach(result.vault, result.dav, result.sync)
                    _navigateTo.value = NavEvent(Routes.DASHBOARD)
                    Log.i(TAG, "auto-unlock succeeded (${if (result.vault.isEncrypted()) "keystore MDK" else "plain server"})")
                }
                is UnlockResult.Form -> {
                    if (result.clearedStoredSession) {
                        Log.e(TAG, "auto-unlock failed; server rejected stored creds, cleared MDK+creds")
                    } else {
                        Log.i(TAG, "auto-unlock unavailable (no stored creds, or no MDK for an encrypted server), waiting for input")
                    }
                    _uiState.value = UiState.Ready(UnlockReady)
                }
                is UnlockResult.Failed -> {
                    Log.e(TAG, "auto-unlock failed: ${result.message}")
                    _uiState.value = UiState.Error(result.message)
                }
            }
        }
    }

    fun submit() {
        val secret = passphrase.value
        val recovery = useRecoveryKey.value
        viewModelScope.launch {
            _uiState.value = UiState.Loading
            when (val result = engine.submit(secret, recovery)) {
                is UnlockResult.Unlocked -> {
                    passphrase.value = ""
                    session.attach(result.vault, result.dav, result.sync)
                    _navigateTo.value = NavEvent(Routes.DASHBOARD)
                    Log.i(TAG, "unlock succeeded (${if (recovery) "recovery key" else "passphrase"})")
                }
                is UnlockResult.Failed -> {
                    Log.e(TAG, "unlock failed: ${result.message}")
                    _uiState.value = UiState.Error(result.message)
                }
                is UnlockResult.Form -> Log.w(TAG, "unexpected Form result from submit; ignoring")
            }
        }
    }

    /** Drops this device's login but keeps Settings, so Setup opens pre-filled. Server data is untouched. */
    fun forgetServer() {
        viewModelScope.launch {
            Log.i(TAG, "forget server requested")
            _uiState.value = UiState.Loading
            runCatchingCancellable {
                forgetSession(sessionVaultStore, session::clear) { Log.i(TAG, it) }
            }.onSuccess {
                passphrase.value = ""
                engine.zeroizeWebdavPassword()
                Log.i(TAG, "forget server done, navigating to setup (back stack cleared)")
                _navigateTo.value = NavEvent(Routes.SETUP, clearBackStack = true)
            }.onFailure { e ->
                Log.e(TAG, "forget server failed", e)
                _uiState.value = UiState.Error(e.message ?: "Could not forget this server")
            }
        }
    }

    fun consumeNavigation() {
        _navigateTo.value = null
    }

    override fun onCleared() {
        engine.zeroizeWebdavPassword()
        super.onCleared()
    }

    private companion object {
        const val TAG = "WS:UNLOCK"
    }
}
