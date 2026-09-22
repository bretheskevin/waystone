package dev.waystone.app.vm

import dev.waystone.data.config.Settings
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WaystoneException
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.vaultInit

private const val PASSPHRASE = "correct horse battery staple"
private const val DAV_PASSWORD = "dav-secret"

private class FakeSettingsProvider(private val settings: Settings) : SettingsProvider {
    override suspend fun load(): Settings = settings
    override suspend fun save(settings: Settings) {}
}

private class FakeSessionVaultStore(
    var mdk: ByteArray? = null,
    var creds: Pair<String, CharArray>? = null,
) : SessionVaultStore {
    val savedCreds = mutableListOf<Pair<String, CharArray>>()
    var wrapCount = 0
    var clearCount = 0

    override suspend fun unwrapMdk(): ByteArray? = mdk
    override suspend fun wrapMdk(mdk: ByteArray): Boolean {
        wrapCount++
        mdk.fill(0)
        return true
    }

    override suspend fun loadCreds(): Pair<String, CharArray>? = creds
    override suspend fun saveCreds(username: String, password: CharArray): Boolean {
        savedCreds += username to password.copyOf()
        password.fill('\u0000')
        return true
    }

    override suspend fun clear() {
        clearCount++
        mdk = null
        creds = null
    }
}

private class FakeWebDav(
    private val keysJson: ByteArray? = null,
    private val rejectGet: Boolean = false,
) : WebDav {
    override fun get(path: String): ByteArray? {
        if (rejectGet) throw WaystoneException.WebDav("GET $path returned 401")
        return keysJson
    }

    override fun put(path: String, body: ByteArray) {}
    override fun exists(path: String): Boolean = true
    override fun propfind(path: String): List<String> = emptyList()
    override fun mkdirP(path: String) {}
}

private fun davProviderOf(dav: WebDav?): DavProvider = object : DavProvider {
    override suspend fun current(): WebDav? = dav
}

private class RecordingDavFactory(private val dav: WebDav) : DavFactory {
    val created = mutableListOf<Triple<String, String, String>>()
    override suspend fun create(serverUrl: String, username: String, password: String): WebDav {
        created += Triple(serverUrl, username, password)
        return dav
    }
}

class UnlockEngineTest {

    private val settings = Settings(deviceId = "device-1", serverUrl = "https://dav.example", username = "alice")

    private fun engine(
        store: FakeSessionVaultStore,
        dav: WebDav = FakeWebDav(),
        factory: DavFactory = RecordingDavFactory(dav),
        current: DavProvider = davProviderOf(dav),
    ) = UnlockEngine(
        settings = FakeSettingsProvider(settings),
        sessionVaultStore = store,
        davProvider = current,
        davFactory = factory,
    )

    @Test
    fun autoUnlockWithoutStoredCredsShowsWebdavField() = runTest {
        val store = FakeSessionVaultStore(mdk = ByteArray(32) { 1 }, creds = null)
        val engine = engine(store)

        val result = engine.attemptAutoUnlock()

        assertEquals(UnlockResult.Form(clearedStoredSession = false), result)
        assertTrue(engine.needsWebdavPassword.value)
        assertEquals(0, store.clearCount)
        assertEquals(0, store.wrapCount)
    }

    @Test
    fun autoUnlockWithoutMdkShowsForm() = runTest {
        val store = FakeSessionVaultStore(mdk = null, creds = null)
        val engine = engine(store)

        val result = engine.attemptAutoUnlock()

        assertEquals(UnlockResult.Form(clearedStoredSession = false), result)
        assertTrue(engine.needsWebdavPassword.value)
    }

    @Test
    fun autoUnlockWithStoredCredsUnlocksFromMdk() = runTest {
        val init = vaultInit(PASSPHRASE)
        val store = FakeSessionVaultStore(mdk = init.vault.exportMdk(), creds = "alice" to DAV_PASSWORD.toCharArray())
        val engine = engine(store, dav = FakeWebDav(keysJson = init.keysJson))

        val result = engine.attemptAutoUnlock()

        assertTrue(result is UnlockResult.Unlocked)
        assertFalse(engine.needsWebdavPassword.value)
        assertEquals(0, store.clearCount)
    }

    @Test
    fun autoUnlockServerRejectingStoredCredsClearsSession() = runTest {
        val init = vaultInit(PASSPHRASE)
        val store = FakeSessionVaultStore(mdk = init.vault.exportMdk(), creds = "alice" to DAV_PASSWORD.toCharArray())
        val engine = engine(store, dav = FakeWebDav(rejectGet = true))

        val result = engine.attemptAutoUnlock()

        assertEquals(UnlockResult.Form(clearedStoredSession = true), result)
        assertTrue(engine.needsWebdavPassword.value)
        assertEquals(1, store.clearCount)
    }

    @Test
    fun submitWithWebdavPasswordUnlocksAndPersistsCreds() = runTest {
        val init = vaultInit(PASSPHRASE)
        val store = FakeSessionVaultStore()
        val factory = RecordingDavFactory(FakeWebDav(keysJson = init.keysJson))
        val engine = engine(store, factory = factory, current = davProviderOf(null))
        engine.attemptAutoUnlock()
        engine.updateWebdavPassword(DAV_PASSWORD)

        val result = engine.submit(PASSPHRASE, recovery = false)

        assertTrue(result is UnlockResult.Unlocked)
        assertEquals(listOf(Triple("https://dav.example", "alice", DAV_PASSWORD)), factory.created)
        assertEquals(listOf("alice"), store.savedCreds.map { it.first })
        assertEquals(listOf(DAV_PASSWORD.toCharArray().toList()), store.savedCreds.map { it.second.toList() })
        assertTrue(engine.webdavPasswordRef().all { it == '\u0000' })
    }

    @Test
    fun submitFailureDoesNotPersistCredsAndZeroizesPassword() = runTest {
        val init = vaultInit(PASSPHRASE)
        val store = FakeSessionVaultStore()
        val engine = engine(store, dav = FakeWebDav(keysJson = init.keysJson), current = davProviderOf(null))
        engine.attemptAutoUnlock()
        engine.updateWebdavPassword(DAV_PASSWORD)

        val result = engine.submit("wrong passphrase", recovery = false)

        assertTrue(result is UnlockResult.Failed)
        assertTrue(store.savedCreds.isEmpty())
        assertTrue(engine.webdavPasswordRef().all { it == '\u0000' })
    }

    @Test
    fun submitWithWebdavFieldVisibleButEmptyRequiresPassword() = runTest {
        val store = FakeSessionVaultStore()
        val engine = engine(store, current = davProviderOf(null))
        engine.attemptAutoUnlock()

        val result = engine.submit(PASSPHRASE, recovery = false)

        assertEquals(UnlockResult.Failed("WebDAV password is required"), result)
    }

    @Test
    fun updateWebdavPasswordZeroizesPreviousValue() = runTest {
        val engine = engine(FakeSessionVaultStore())
        engine.updateWebdavPassword("first")
        val first = engine.webdavPasswordRef()

        engine.updateWebdavPassword("second")

        assertTrue(first.all { it == '\u0000' })
        assertEquals("second", engine.webdavPasswordRef().concatToString())
    }

    @Test
    fun recoveryUnlockUsesRecoveryKey() = runTest {
        val init = vaultInit(PASSPHRASE)
        val store = FakeSessionVaultStore()
        val factory = RecordingDavFactory(FakeWebDav(keysJson = init.keysJson))
        val engine = engine(store, factory = factory, current = davProviderOf(null))
        engine.attemptAutoUnlock()
        engine.updateWebdavPassword(DAV_PASSWORD)

        val result = engine.submit(init.recoveryHex, recovery = true)

        assertTrue(result is UnlockResult.Unlocked)
        assertEquals(listOf("alice"), store.savedCreds.map { it.first })
        assertEquals(listOf(DAV_PASSWORD.toCharArray().toList()), store.savedCreds.map { it.second.toList() })
    }

    @Test
    fun submitWithoutStoredCredsOrPasswordReportsServerNotConfigured() = runTest {
        val store = FakeSessionVaultStore(creds = null)
        val engine = engine(store, current = davProviderOf(null))

        val result = engine.submit(PASSPHRASE, recovery = false)

        assertEquals(UnlockResult.Failed("Server is not configured"), result)
    }

    @Test
    fun vaultUnlockedFromMdkMatchesExportedMdk() = runTest {
        val init = vaultInit(PASSPHRASE)
        val store = FakeSessionVaultStore(mdk = init.vault.exportMdk(), creds = "alice" to DAV_PASSWORD.toCharArray())
        val engine = engine(store, dav = FakeWebDav(keysJson = init.keysJson))

        val result = engine.attemptAutoUnlock() as UnlockResult.Unlocked

        // Round-trip: unlock a fresh handle from the stored MDK and re-export.
        val fromMdk = Vault.unlockWithPassphrase(PASSPHRASE, init.keysJson)
        assertArrayEquals(fromMdk.exportMdk(), result.vault.exportMdk())
    }
}
