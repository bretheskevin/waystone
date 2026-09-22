package dev.waystone.app.vm

import android.app.Application
import android.util.Log
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
    sessionVaultStore: SessionVaultStore,
    davProvider: DavProvider,
    davFactory: DavFactory,
    private val session: SessionViewModel,
) : AndroidViewModel(application) {

    private val engine = UnlockEngine(settings, sessionVaultStore, davProvider, davFactory)

    private val _uiState = MutableStateFlow<UiState<UnlockReady>>(UiState.Loading)
    val uiState: StateFlow<UiState<UnlockReady>> = _uiState.asStateFlow()

    private val _navigateTo = MutableStateFlow<String?>(null)
    val navigateTo: StateFlow<String?> = _navigateTo.asStateFlow()

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
                    _navigateTo.value = "dashboard"
                    Log.i(TAG, "auto-unlock succeeded from keystore MDK")
                }
                is UnlockResult.Form -> {
                    if (result.clearedStoredSession) {
                        Log.e(TAG, "auto-unlock failed; server rejected stored creds, cleared MDK+creds")
                    } else {
                        Log.i(TAG, "auto-unlock unavailable (no stored MDK or creds), waiting for input")
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
                    _navigateTo.value = "dashboard"
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
