package dev.waystone.data.session

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FakeSessionStoreTest {

    @Test
    fun `wrap unwrap round-trips the mdk`() {
        val store = FakeSessionStore()
        val mdk = byteArrayOf(1, 2, 3, 4)
        assertTrue(store.wrapMdk(mdk))
        assertArrayEquals(byteArrayOf(1, 2, 3, 4), store.unwrapMdk())
    }

    @Test
    fun `wrapMdk zeroizes the caller's array`() {
        val store = FakeSessionStore()
        val mdk = byteArrayOf(9, 9, 9)
        store.wrapMdk(mdk)
        assertArrayEquals(ByteArray(3), mdk)
    }

    @Test
    fun `unwrap after clear returns null`() {
        val store = FakeSessionStore()
        store.wrapMdk(byteArrayOf(1))
        store.clear()
        assertNull(store.unwrapMdk())
    }

    @Test
    fun `unwrap with no stored mdk returns null`() {
        assertNull(FakeSessionStore().unwrapMdk())
    }

    @Test
    fun `creds round-trip and password is zeroized by saveCreds`() {
        val store = FakeSessionStore()
        val password = charArrayOf('s', 'e', 'c', 'r', 'e', 't')
        assertTrue(store.saveCreds("alice", password))

        // saveCreds zeroizes the caller's CharArray.
        assertTrue(password.all { it == '\u0000' })

        val (user, pass) = store.loadCreds()!!
        assertEquals("alice", user)
        assertEquals("secret", pass.concatToString())
    }

    @Test
    fun `loadCreds with no stored creds returns null`() {
        assertNull(FakeSessionStore().loadCreds())
    }

    @Test
    fun `loadCreds after clear returns null`() {
        val store = FakeSessionStore()
        store.saveCreds("bob", charArrayOf('x'))
        store.clear()
        assertNull(store.loadCreds())
    }

    @Test
    fun `corrupt blob yields null unwrap so caller falls back to passphrase`() {
        val store = FakeSessionStore()
        store.wrapMdk(byteArrayOf(5))
        store.simulateCorruption = true
        assertNull(store.unwrapMdk())
        assertNull(store.loadCreds())
    }
}
