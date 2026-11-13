package dev.waystone.app.vm

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SessionForgetTest {

    @Test
    fun `forgetSession clears the stored session and the in-memory session`() = runTest {
        val store = FakeSessionVaultStore(mdk = ByteArray(32) { 1 }, creds = "alice" to "pw".toCharArray())
        var sessionCleared = 0

        forgetSession(store, onSessionCleared = { sessionCleared++ }, log = {})

        assertEquals(1, store.clearCount)
        assertEquals(1, sessionCleared)
        assertNull(store.mdk)
        assertNull(store.creds)
    }

    @Test
    fun `forgetSession logs entry and outcome`() = runTest {
        val lines = mutableListOf<String>()

        forgetSession(FakeSessionVaultStore(), onSessionCleared = {}, log = { lines += it })

        assertTrue(lines.size >= 2)
        assertTrue(lines.last().contains("cleared"))
    }
}
