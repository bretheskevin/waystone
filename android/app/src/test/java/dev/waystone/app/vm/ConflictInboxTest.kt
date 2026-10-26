package dev.waystone.app.vm

import dev.waystone.data.sync.ConflictEntry
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.NormalizedSave

@OptIn(ExperimentalCoroutinesApi::class)
class ConflictInboxTest {
    private fun save(k: String) = NormalizedSave("jksv", "switch", k, k, null, null, null, Confidence.STRONG,
        "main", "native", "switch/$k/main", true, "", emptyList())
    private fun entry(k: String) = ConflictEntry(save(k), "l", "r", "dev2", "2026", "2026")

    @Test
    fun `entries are emitted progressively in scan order`() = runTest {
        val gates = listOf("A", "B", "C").associateWith { CompletableDeferred<Unit>() }
        val inbox = ConflictInbox(
            scope = this,
            loadSaves = { listOf(save("A"), save("B"), save("C")) },
            scanOne = { s -> gates.getValue(s.gameKey).await(); if (s.gameKey == "B") null else entry(s.gameKey) },
            onKeepLocal = {}, onKeepRemote = {},
        )
        inbox.startScan(); runCurrent()
        assertTrue(inbox.state.value.scanning)
        assertEquals(3, inbox.state.value.total)
        gates.getValue("A").complete(Unit); runCurrent()
        assertEquals(listOf("A"), inbox.state.value.entries.map { it.save.gameKey })
        gates.getValue("B").complete(Unit); gates.getValue("C").complete(Unit); advanceUntilIdle()
        assertEquals(listOf("A", "C"), inbox.state.value.entries.map { it.save.gameKey })
        assertFalse(inbox.state.value.scanning)
        assertTrue(inbox.state.value.completed)
        assertEquals(3, inbox.state.value.scanned)
    }

    @Test
    fun `the same game from two sources is listed once`() = runTest {
        val inbox = ConflictInbox(this, { listOf(save("A"), save("A")) }, { entry(it.gameKey) }, onKeepLocal = {}, onKeepRemote = {})
        inbox.startScan(); advanceUntilIdle()
        assertEquals(listOf("A"), inbox.state.value.entries.map { it.save.gameKey })
        assertEquals(2, inbox.state.value.scanned)
    }

    @Test
    fun `an empty library completes as finishedEmpty`() = runTest {
        val inbox = ConflictInbox(this, { emptyList() }, { null }, onKeepLocal = {}, onKeepRemote = {})
        assertFalse(inbox.state.value.finishedEmpty)   // not started yet
        inbox.startScan(); advanceUntilIdle()
        assertTrue(inbox.state.value.finishedEmpty)
    }

    @Test
    fun `resolutions run sequentially while the scan continues and remove entries`() = runTest {
        val order = mutableListOf<String>()
        val firstGate = CompletableDeferred<Unit>()
        val scanGate = CompletableDeferred<Unit>()
        val inbox = ConflictInbox(
            scope = this,
            loadSaves = { listOf(save("A"), save("B"), save("C")) },
            scanOne = { s -> if (s.gameKey == "C") scanGate.await(); entry(s.gameKey) },
            onKeepLocal = { e -> order += "local:${e.save.gameKey}"; if (e.save.gameKey == "A") firstGate.await() },
            onKeepRemote = { e -> order += "remote:${e.save.gameKey}" },
        )
        inbox.startScan(); runCurrent()
        inbox.resolve("switch/A/main", keepRemote = false)
        inbox.resolve("switch/B/main", keepRemote = true)
        runCurrent()
        assertEquals(listOf("local:A"), order)               // B waits for A (queue)
        assertTrue(inbox.state.value.busy.containsAll(setOf("switch/A/main", "switch/B/main")))
        firstGate.complete(Unit); runCurrent()
        assertEquals(listOf("local:A", "remote:B"), order)
        assertTrue(inbox.state.value.scanning)               // scan still running
        scanGate.complete(Unit); advanceUntilIdle()
        assertEquals(listOf("C"), inbox.state.value.entries.map { it.save.gameKey })
        assertTrue(inbox.state.value.busy.isEmpty())
    }

    @Test
    fun `failed resolution keeps the entry and reports the error`() = runTest {
        val inbox = ConflictInbox(this, { listOf(save("A")) }, { entry(it.gameKey) },
            onKeepLocal = { error("boom") }, onKeepRemote = {})
        inbox.startScan(); advanceUntilIdle()
        inbox.resolve("switch/A/main", keepRemote = false); advanceUntilIdle()
        assertEquals(1, inbox.state.value.entries.size)
        assertTrue(inbox.state.value.error!!.contains("boom"))
    }
}
