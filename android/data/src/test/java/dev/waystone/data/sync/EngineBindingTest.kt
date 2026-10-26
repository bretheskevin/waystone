package dev.waystone.data.sync

import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PlanAction
import uniffi.waystone_mobile.engineForcedPush
import uniffi.waystone_mobile.readRemoteHeads
import uniffi.waystone_mobile.vaultInit

class EngineBindingTest {
    private fun save(key: String, content: String, mtime: String = "2026-01-01T00:00:00Z") = NormalizedSave(
        source = "jksv", system = "switch", gameKey = key, displayName = key, titleId = key,
        serial = null, romCrc = null, confidence = Confidence.STRONG, slot = "main", kind = "native",
        groupKey = "switch/$key/main", portable = true, mtime = mtime,
        files = listOf(FileEntry("save.dat", content.toByteArray())),
    )

    @Test
    fun `execute streams Started, phases and Done and a pull writes this device's head`() = runBlocking {
        val v = vaultInit("b1").vault
        val dav = FakeWebDav()
        val dev1 = SyncRepository(v, dav, "dev1")
        val dev2 = SyncRepository(v, dav, "dev2")
        val old = save("G", "old")
        dev1.pushOne(old)
        dev2.pushOne(old.copy(files = listOf(FileEntry("save.dat", "new".toByteArray())), mtime = "2026-02-01T00:00:00Z"))
        val plan = dev1.plan(listOf(old, save("FRESH", "x")), ConflictPolicy.PROMPT)
        assertEquals(listOf(PlanAction.PULL, PlanAction.PUSH), plan.map { it.action })

        val restored = mutableListOf<Int>()
        val events = dev1.execute(plan, write = { i, _, _ -> restored += i; true }).toList()

        assertTrue(events.first() is SyncProgress.Started)
        val done = events.last() as SyncProgress.Done
        assertEquals(1u, done.summary.pulled)
        assertEquals(1u, done.summary.pushed)
        assertEquals(listOf(0), restored)
        assertEquals("Done — 1 uploaded, 1 downloaded", done.summary.headline())
        val heads = readRemoteHeads(v, old, dav)
        assertTrue(heads.any { it.deviceId == "dev1" && it.mtime == "2026-02-01T00:00:00Z" })
    }

    @Test
    fun `cancel stops after the current save`() = runBlocking {
        val v = vaultInit("b2").vault
        val repo = SyncRepository(v, FakeWebDav(), "dev1")
        val plan = listOf("A", "B", "C").mapNotNull { engineForcedPush(save(it, it)) }
        var n = 0
        val done = repo.execute(plan, write = { _, _, _ -> true }, cancelled = { n++ >= 1 }).toList().last() as SyncProgress.Done
        assertTrue(done.summary.cancelled)
    }

    @Test
    fun `a throwing restore counts as an error and the run continues`() = runBlocking {
        val v = vaultInit("b3").vault
        val dav = FakeWebDav()
        val dev1 = SyncRepository(v, dav, "dev1")
        val old = save("G", "old")
        dev1.pushOne(old)
        SyncRepository(v, dav, "dev2")
            .pushOne(old.copy(files = listOf(FileEntry("save.dat", "new".toByteArray())), mtime = "2026-02-01T00:00:00Z"))
        val plan = dev1.plan(listOf(old, save("FRESH", "x")), ConflictPolicy.PROMPT)

        val done = dev1.execute(plan, write = { _, _, _ -> error("disk full") }).toList().last() as SyncProgress.Done

        assertEquals(1u, done.summary.errors)
        assertEquals(1u, done.summary.pushed)
    }

    @Test
    fun `forced push refuses remote-only saves`() {
        assertNull(engineForcedPush(save("R", "x").copy(files = emptyList())))
    }
}
