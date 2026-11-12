package dev.waystone.app.vm

import dev.waystone.app.log.Log
import dev.waystone.data.sync.runCatchingCancellable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.KeysMode
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.VaultInit
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.keysMode
import uniffi.waystone_mobile.vaultInit
import uniffi.waystone_mobile.vaultPlain

sealed interface SetupResult {
    /** [recoveryHex] is null for a plain (unencrypted) server. */
    data class Created(val vault: Vault, val recoveryHex: String?) : SetupResult

    data class JoinedPlain(val vault: Vault) : SetupResult

    data object VaultExists : SetupResult

    data class Failed(val message: String) : SetupResult
}

/** Pure setup flow; a null passphrase creates (or joins) an unencrypted server. */
class SetupEngine(
    private val sessionVaultStore: SessionVaultStore,
    private val keysModeFn: (ByteArray) -> KeysMode = { keysMode(it) },
    private val vaultPlainFn: () -> Vault = { vaultPlain() },
    private val vaultInitFn: (String) -> VaultInit = { vaultInit(it) },
    private val log: (String) -> Unit = { Log.i(TAG, it) },
) {
    suspend fun run(dav: WebDav, username: String, password: CharArray, passphrase: String?): SetupResult =
        runCatchingCancellable {
            val existing = withContext(Dispatchers.IO) { dav.get("/keys.json") }
            when {
                existing != null -> join(existing, username, password)
                passphrase == null -> createPlain(dav, username, password)
                else -> createEncrypted(dav, username, password, passphrase)
            }
        }.getOrElse { e ->
            log("[vault] setup failed: ${e.javaClass.simpleName}: ${e.message}")
            SetupResult.Failed(e.message ?: "Setup failed")
        }

    private suspend fun join(keysJson: ByteArray, username: String, password: CharArray): SetupResult {
        val mode = try {
            detectKeysMode(keysJson, keysModeFn, log)
        } catch (e: Exception) {
            log("[vault] keys.json on server is unrecognized: ${e.message}")
            return SetupResult.Failed("Unrecognized keys.json on server")
        }
        return when (mode) {
            KeysMode.PLAIN -> {
                val vault = vaultPlainFn()
                savePlainSession(username, password)
                log("[vault] joined existing plain server")
                SetupResult.JoinedPlain(vault)
            }
            KeysMode.ENCRYPTED -> {
                log("[vault] encrypted vault already exists on server")
                SetupResult.VaultExists
            }
        }
    }

    private suspend fun createPlain(dav: WebDav, username: String, password: CharArray): SetupResult {
        log("[vault] keys.json absent, creating plain server")
        val vault = vaultPlainFn()
        withContext(Dispatchers.IO) {
            dav.mkdirP("/")
            dav.put("/keys.json", vault.keysJson())
        }
        savePlainSession(username, password)
        log("[vault] plain server created")
        return SetupResult.Created(vault, null)
    }

    // Drops a previous encrypted session's MDK: UnlockEngine refuses a plain server while one is held.
    private suspend fun savePlainSession(username: String, password: CharArray) {
        sessionVaultStore.clear()
        sessionVaultStore.saveCreds(username, password)
    }

    private suspend fun createEncrypted(
        dav: WebDav,
        username: String,
        password: CharArray,
        passphrase: String,
    ): SetupResult {
        log("[vault] keys.json absent, creating encrypted vault")
        val init = withContext(Dispatchers.IO) {
            dav.mkdirP("/")
            vaultInitFn(passphrase).also { dav.put("/keys.json", it.keysJson) }
        }
        val mdk = init.vault.exportMdk()
        if (mdk == null || !sessionVaultStore.wrapMdk(mdk)) {
            log("[vault] wrapping the session key failed")
            return SetupResult.Failed(SESSION_KEY_ERROR)
        }
        sessionVaultStore.saveCreds(username, password)
        log("[vault] encrypted vault created")
        return SetupResult.Created(init.vault, init.recoveryHex)
    }

    private companion object {
        const val TAG = "WS:SETUP"
    }
}

internal fun KeysMode.label(): String = name.lowercase()

internal const val SESSION_KEY_ERROR = "Failed to store the session key"

/** keys.json mode probe (logged) shared by setup and unlock; throws on an unrecognized file. */
internal fun detectKeysMode(keysJson: ByteArray, keysModeFn: (ByteArray) -> KeysMode, log: (String) -> Unit): KeysMode =
    keysModeFn(keysJson).also { log("[vault] keys.json mode=${it.label()}") }
