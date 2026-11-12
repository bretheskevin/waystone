package dev.waystone.app.vm

import dev.waystone.app.log.Log
import dev.waystone.data.sync.SyncRepository
import dev.waystone.data.sync.runCatchingCancellable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.KeysMode
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.keysMode
import uniffi.waystone_mobile.vaultFromMdk

private const val NO_VAULT_ERROR = "No vault on this server — run Setup first"
internal const val DOWNGRADE_ERROR =
    "This server now reports no encryption, but this device uses an encrypted vault. Run Setup again to switch."

sealed interface UnlockResult {
    data class Unlocked(val vault: Vault, val dav: WebDav, val sync: SyncRepository) : UnlockResult

    /** Show the manual unlock form; true when the stored MDK+creds were cleared (fresh unlock required). */
    data class Form(val clearedStoredSession: Boolean) : UnlockResult

    data class Failed(val message: String) : UnlockResult
}

/**
 * Pure unlock flow, seam-injected so it stays unit-testable without Robolectric.
 * The WebDAV password lives only in [webdavPassword], never in state objects, and is
 * zeroized on submit and by [zeroizeWebdavPassword] (called on navigation-away).
 */
class UnlockEngine(
    private val settings: SettingsProvider,
    private val sessionVaultStore: SessionVaultStore,
    private val davProvider: DavProvider,
    private val davFactory: DavFactory,
    private val vaultFromMdkFn: (ByteArray) -> Vault = { vaultFromMdk(it) },
    private val keysModeFn: (ByteArray) -> KeysMode = { keysMode(it) },
    private val vaultOpenFn: (ByteArray, String?, Boolean) -> Vault = { keys, secret, recovery ->
        Vault.open(keys, secret, recovery)
    },
    private val log: (String) -> Unit = { Log.i(TAG, it) },
) {
    val needsWebdavPassword = MutableStateFlow(false)

    private var webdavPassword = charArrayOf()

    fun updateWebdavPassword(value: String) {
        webdavPassword.fill('\u0000')
        webdavPassword = value.toCharArray()
    }

    fun zeroizeWebdavPassword() {
        webdavPassword.fill('\u0000')
        webdavPassword = charArrayOf()
    }

    suspend fun attemptAutoUnlock(): UnlockResult {
        val creds = runCatchingCancellable { sessionVaultStore.loadCreds() }.getOrNull()
        if (creds == null) {
            needsWebdavPassword.value = true
            return UnlockResult.Form(clearedStoredSession = false)
        }
        creds.second.fill('\u0000')
        return runCatchingCancellable {
            val dav = davProvider.current() ?: error("Server is not configured")
            // Probe the server so rejected stored creds (401) surface here, not mid-sync.
            val keysJson = withContext(Dispatchers.IO) { dav.get("/keys.json") }
                ?: error(NO_VAULT_ERROR)
            val vault = when (detectKeysMode(keysJson, keysModeFn, log)) {
                KeysMode.PLAIN -> {
                    if (holdsEncryptedSession()) return UnlockResult.Form(clearedStoredSession = false)
                    vaultOpenFn(keysJson, null, false).also {
                        log("[vault] auto-unlock: plain server, no session key needed")
                    }
                }
                KeysMode.ENCRYPTED -> {
                    val mdk = runCatchingCancellable { sessionVaultStore.unwrapMdk() }.getOrNull()
                        ?: return UnlockResult.Form(clearedStoredSession = false).also {
                            log("[vault] auto-unlock: no stored session key, waiting for passphrase")
                        }
                    try {
                        withContext(Dispatchers.IO) { vaultFromMdkFn(mdk) }
                    } finally {
                        mdk.fill(0)
                    }
                }
            }
            UnlockResult.Unlocked(vault, dav, SyncRepository(vault, dav, settings.load().deviceId))
        }.getOrElse { e ->
            log("[vault] auto-unlock failed: ${e.javaClass.simpleName}; clearing stored session")
            sessionVaultStore.clear()
            needsWebdavPassword.value = true
            UnlockResult.Form(clearedStoredSession = true)
        }
    }

    suspend fun submit(secret: String, recovery: Boolean): UnlockResult {
        val loaded = settings.load()
        val havePassword = webdavPassword.isNotEmpty()
        val dav = when {
            havePassword -> {
                if (loaded.serverUrl.isBlank()) return UnlockResult.Failed("Server is not configured")
                davFactory.create(loaded.serverUrl, loaded.username ?: "", String(webdavPassword))
            }
            needsWebdavPassword.value -> return UnlockResult.Failed("WebDAV password is required")
            else -> davProvider.current() ?: return UnlockResult.Failed("Server is not configured")
        }
        return try {
            runCatchingCancellable {
                val keysJson = withContext(Dispatchers.IO) { dav.get("/keys.json") }
                    ?: return UnlockResult.Failed(NO_VAULT_ERROR)
                val mode = detectKeysMode(keysJson, keysModeFn, log)
                if (mode == KeysMode.ENCRYPTED && secret.isEmpty()) {
                    log("[vault] unlock refused: encrypted server, no ${if (recovery) "recovery key" else "passphrase"} entered")
                    return UnlockResult.Failed(if (recovery) "Recovery key is required" else "Passphrase is required")
                }
                if (mode == KeysMode.PLAIN && holdsEncryptedSession()) return UnlockResult.Failed(DOWNGRADE_ERROR)
                val vault = withContext(Dispatchers.IO) {
                    vaultOpenFn(keysJson, secret.takeIf { mode == KeysMode.ENCRYPTED }, recovery)
                }
                vault.exportMdk()?.let { mdk ->
                    if (!sessionVaultStore.wrapMdk(mdk)) {
                        log("[vault] unlock: wrapping the session key failed")
                        return UnlockResult.Failed(SESSION_KEY_ERROR)
                    }
                }
                if (havePassword) {
                    sessionVaultStore.saveCreds(loaded.username ?: "", webdavPassword.copyOf())
                }
                log("[vault] unlock: ${mode.label()} vault opened")
                UnlockResult.Unlocked(vault, dav, SyncRepository(vault, dav, loaded.deviceId))
            }.getOrElse { e ->
                log("[vault] unlock failed: ${e.javaClass.simpleName}")
                UnlockResult.Failed(e.message ?: "Unlock failed")
            }
        } finally {
            zeroizeWebdavPassword()
        }
    }

    // A stored MDK means this device synced an encrypted vault: a server that now reports plain
    // must not silently downgrade it to plaintext uploads (only a fresh Setup clears the MDK).
    private suspend fun holdsEncryptedSession(): Boolean {
        val mdk = runCatchingCancellable { sessionVaultStore.unwrapMdk() }.getOrNull() ?: return false
        mdk.fill(0)
        log("[vault] refused: server reports unencrypted but this device holds an encrypted session")
        return true
    }

    internal fun webdavPasswordRef(): CharArray = webdavPassword

    private companion object {
        const val TAG = "WS:UNLOCK"
    }
}
