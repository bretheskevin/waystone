package dev.waystone.app.vm

import dev.waystone.data.sync.SyncRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.vaultFromMdk

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
    private val unlockWithPassphrase: (String, ByteArray) -> Vault = { secret, keys ->
        Vault.unlockWithPassphrase(secret, keys)
    },
    private val unlockWithRecovery: (String, ByteArray) -> Vault = { secret, keys ->
        Vault.unlockWithRecovery(secret, keys)
    },
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
        val creds = runCatching { sessionVaultStore.loadCreds() }.getOrNull()
        val mdk = runCatching { sessionVaultStore.unwrapMdk() }.getOrNull()
        if (creds == null) {
            needsWebdavPassword.value = true
        } else {
            creds.second.fill('\u0000')
        }
        if (mdk == null) {
            return UnlockResult.Form(clearedStoredSession = false)
        }
        if (creds == null) {
            // Stored MDK is still valid; a successful manual unlock re-wraps it.
            mdk.fill(0)
            return UnlockResult.Form(clearedStoredSession = false)
        }
        val result = runCatching {
            val vault = withContext(Dispatchers.IO) { vaultFromMdkFn(mdk) }
            val dav = davProvider.current() ?: error("Server is not configured")
            // Probe the server so rejected stored creds (401) surface here, not mid-sync.
            withContext(Dispatchers.IO) { dav.get("/keys.json") }
                ?: error("No vault on this server — run Setup first")
            UnlockResult.Unlocked(vault, dav, SyncRepository(vault, dav, settings.load().deviceId))
        }.getOrElse { e ->
            UnlockResult.Form(clearedStoredSession = true).also {
                sessionVaultStore.clear()
                needsWebdavPassword.value = true
            }
        }
        mdk.fill(0)
        return result
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
            val keysJson = withContext(Dispatchers.IO) { dav.get("/keys.json") }
                ?: return UnlockResult.Failed("No vault on this server — run Setup first")
            val vault = withContext(Dispatchers.IO) {
                if (recovery) unlockWithRecovery(secret, keysJson) else unlockWithPassphrase(secret, keysJson)
            }
            if (!sessionVaultStore.wrapMdk(vault.exportMdk())) {
                return UnlockResult.Failed("Failed to store the session key")
            }
            if (havePassword) {
                sessionVaultStore.saveCreds(loaded.username ?: "", webdavPassword.copyOf())
            }
            UnlockResult.Unlocked(vault, dav, SyncRepository(vault, dav, loaded.deviceId))
        } catch (e: Exception) {
            UnlockResult.Failed(e.message ?: "Unlock failed")
        } finally {
            zeroizeWebdavPassword()
        }
    }

    internal fun webdavPasswordRef(): CharArray = webdavPassword
}
