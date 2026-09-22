package dev.waystone.app.vm

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import dev.waystone.data.sync.SyncRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav

sealed interface SessionState {
    data object NoVault : SessionState
    data object Locked : SessionState
    data object Unlocked : SessionState
}

class SessionViewModel(application: Application) : AndroidViewModel(application) {

    private val _sessionState = MutableStateFlow<SessionState>(SessionState.NoVault)
    val sessionState: StateFlow<SessionState> = _sessionState.asStateFlow()

    // Handles never enter state objects; only the enum does.
    private var vault: Vault? = null
    private var dav: WebDav? = null
    private var sync: SyncRepository? = null

    fun unlockedVault(): Vault? = vault

    fun currentDav(): WebDav? = dav

    fun syncRepo(): SyncRepository? = sync

    fun attach(newVault: Vault, newDav: WebDav, newSync: SyncRepository) {
        vault = newVault
        dav = newDav
        sync = newSync
        _sessionState.value = SessionState.Unlocked
        Log.i(TAG, "session attached, state=Unlocked")
    }

    fun clear() {
        vault = null
        dav = null
        sync = null
        _sessionState.value = SessionState.NoVault
        Log.i(TAG, "session cleared, state=NoVault")
    }

    private companion object {
        const val TAG = "WS:SESSION"
    }
}
