package dev.waystone.app.vm

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.vaultInit
import uniffi.waystone_mobile.vaultPlain

private const val SETUP_PASSPHRASE = "correct horse battery staple"

class SetupEngineTest {

    private fun engine(store: FakeSessionVaultStore) = SetupEngine(store, log = {})

    @Test
    fun absentKeysWithoutPassphraseCreatesPlainServer() = runTest {
        val store = FakeSessionVaultStore()
        val dav = FakeWebDav(keysJson = null)

        val result = engine(store).run(dav, "alice", "pw".toCharArray(), passphrase = null)

        val created = result as SetupResult.Created
        assertNull(created.recoveryHex)
        assertFalse(created.vault.isEncrypted())
        assertEquals(listOf("/keys.json"), dav.puts.map { it.first })
        assertArrayEquals(vaultPlain().keysJson(), dav.puts.single().second)
        assertEquals(0, store.wrapCount)
        assertEquals(listOf("alice"), store.savedCreds.map { it.first })
    }

    @Test
    fun absentKeysWithPassphraseCreatesEncryptedVault() = runTest {
        val store = FakeSessionVaultStore()
        val dav = FakeWebDav(keysJson = null)

        val result = engine(store).run(dav, "alice", "pw".toCharArray(), SETUP_PASSPHRASE)

        val created = result as SetupResult.Created
        assertNotNull(created.recoveryHex)
        assertTrue(created.vault.isEncrypted())
        assertEquals(listOf("/keys.json"), dav.puts.map { it.first })
        assertEquals(1, store.wrapCount)
        assertEquals(listOf("alice"), store.savedCreds.map { it.first })
    }

    @Test
    fun existingPlainServerIsJoinedWithoutPut() = runTest {
        val store = FakeSessionVaultStore()
        val dav = FakeWebDav(keysJson = vaultPlain().keysJson())

        val result = engine(store).run(dav, "alice", "pw".toCharArray(), passphrase = null)

        assertTrue(result is SetupResult.JoinedPlain)
        assertTrue(dav.puts.isEmpty())
        assertEquals(0, store.wrapCount)
        assertEquals(listOf("alice"), store.savedCreds.map { it.first })
    }

    @Test
    fun plainSetupDropsAStaleEncryptedSessionKey() = runTest {
        val store = FakeSessionVaultStore(mdk = ByteArray(32) { 7 })

        engine(store).run(FakeWebDav(keysJson = vaultPlain().keysJson()), "alice", "pw".toCharArray(), null)

        assertNull(store.mdk)
        assertEquals(1, store.clearCount)
        assertEquals(listOf("alice"), store.savedCreds.map { it.first })
    }

    @Test
    fun existingEncryptedServerReportsVaultExists() = runTest {
        val store = FakeSessionVaultStore()
        val dav = FakeWebDav(keysJson = vaultInit(SETUP_PASSPHRASE).keysJson)

        val result = engine(store).run(dav, "alice", "pw".toCharArray(), passphrase = null)

        assertEquals(SetupResult.VaultExists, result)
        assertTrue(dav.puts.isEmpty())
        assertTrue(store.savedCreds.isEmpty())
    }

    @Test
    fun unrecognizedKeysJsonFails() = runTest {
        val store = FakeSessionVaultStore()
        val dav = FakeWebDav(keysJson = "not json".toByteArray())

        val result = engine(store).run(dav, "alice", "pw".toCharArray(), passphrase = null)

        assertEquals(SetupResult.Failed("Unrecognized keys.json on server"), result)
        assertTrue(dav.puts.isEmpty())
    }
}
