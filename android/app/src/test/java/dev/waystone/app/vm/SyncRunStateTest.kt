package dev.waystone.app.vm

import uniffi.waystone_mobile.SyncPhase
import dev.waystone.data.sync.SyncProgress
import uniffi.waystone_mobile.SyncSummary
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
        s = s.reduce(SyncProgress.Item(1, 3, save.groupKey, save.displayName, SyncPhase.PULLING))
        assertEquals("Game", s.currentName)
        assertEquals(SyncPhase.PULLING, s.phase)
        assertEquals(2, s.position)
        assertEquals(2f / 3f, s.fraction, 0.001f)
        s = s.reduce(SyncProgress.Done(SyncSummary(
            pushed = 0u, pulled = 1u, inSync = 0u, conflicts = 0u, errors = 0u, errorMessages = emptyList(), cancelled = false,
        )))
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
