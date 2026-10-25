package dev.waystone.app.vm

import dev.waystone.data.sync.SyncPhase
import dev.waystone.data.sync.SyncProgress
import dev.waystone.data.sync.SyncSummary
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.NormalizedSave

class SyncRunStateTest {
    private val save = NormalizedSave("jksv", "switch", "G", "Game", null, null, null, Confidence.STRONG,
        "main", "native", "switch/G/main", true, "", emptyList())

    @Test
    fun `reduces progress events into display state`() {
        var s = SyncRunState()
        assertTrue(s.running)
        s = s.reduce(SyncProgress.Started(3))
        assertEquals(3, s.total)
        s = s.reduce(SyncProgress.Item(1, 3, save, SyncPhase.Pulling))
        assertEquals("Game", s.currentName)
        assertEquals(SyncPhase.Pulling, s.phase)
        assertEquals(2, s.position)
        assertEquals(2f / 3f, s.fraction, 0.001f)
        s = s.reduce(SyncProgress.Done(SyncSummary(pulled = 1)))
        assertFalse(s.running)
        assertEquals(1f, s.fraction, 0.001f)
    }

    @Test
    fun `cancel request keeps running until Done`() {
        val s = SyncRunState().reduce(SyncProgress.Started(2)).copy(cancelling = true)
        assertTrue(s.running)
        assertTrue(s.cancelling)
    }
}
