package dev.waystone.data.sync

import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.vaultInit

class SyncEngineTest {

    private fun save(key: String, content: String, mtime: String = "2026-01-01T00:00:00Z") = NormalizedSave(
        source = "jksv", system = "switch", gameKey = key, displayName = key, titleId = key,
        serial = null, romCrc = null, confidence = Confidence.STRONG, slot = "main", kind = "native",
        groupKey = "switch/$key/main", portable = true, mtime = mtime,
        files = listOf(FileEntry("save.dat", content.toByteArray())),
    )

    private fun vault(p: String): Vault = vaultInit(p).vault

    @Test
    fun `plan covers push, in-sync, pull, conflict and captures per-save errors`() = runBlocking {
        val v = vault("e1")
        val dav = FakeWebDav()
        val dev1 = SyncRepository(v, dav, "dev1")
        val dev2 = SyncRepository(v, dav, "dev2")
        val synced = save("SYNCED", "same")
        dev1.pushOne(synced)
        val behind = save("BEHIND", "old", "2026-01-01T00:00:00Z")
        dev1.pushOne(behind)
        dev2.pushOne(behind.copy(files = listOf(FileEntry("save.dat", "new".toByteArray())), mtime = "2026-01-05T00:00:00Z"))
        val conflict = save("CONFLICT", "mine")
        dev2.pushOne(conflict.copy(files = listOf(FileEntry("save.dat", "theirs".toByteArray()))))
        val fresh = save("FRESH", "local only")
        val broken = save("BROKEN", "x").copy(system = "megadrive")

        val plan = dev1.plan(listOf(fresh, synced, behind, conflict, broken), ConflictPolicy.PROMPT)

        assertEquals(
            listOf(PlanAction.Push, PlanAction.InSync, PlanAction.Pull, PlanAction.Conflict, PlanAction.Error),
            plan.entries.map { it.action },
        )
        assertTrue(plan.entries[2].pullHash!!.isNotEmpty())
        assertTrue(plan.entries[3].decision is SyncDecision.ConflictNeedsInput)
        assertTrue(plan.entries[4].error!!.isNotEmpty())
        assertEquals(SourceStatus.Behind, plan.entries[2].status)
    }

    @Test
    fun `newest-wins conflict with newer remote becomes a pull of the merged head`() = runBlocking {
        val v = vault("e2")
        val dav = FakeWebDav()
        val dev2 = SyncRepository(v, dav, "dev2")
        val local = save("NW", "mine", "2026-01-01T00:00:00Z")
        dev2.pushOne(local.copy(files = listOf(FileEntry("save.dat", "theirs".toByteArray())), mtime = "2026-02-01T00:00:00Z"))

        val entry = SyncRepository(v, dav, "dev1").planOne(local, ConflictPolicy.NEWEST_WINS)

        assertEquals(PlanAction.Pull, entry.action)
        assertTrue(entry.decision is SyncDecision.ConflictResolved)
    }

    @Test
    fun `remote-only save plans a pull and never a push`() = runBlocking {
        val v = vault("e3")
        val dav = FakeWebDav()
        val other = SyncRepository(v, dav, "dev2")
        val full = save("ROMONLY", "remote")
        other.pushOne(full)

        val entry = SyncRepository(v, dav, "dev1").planOne(full.copy(files = emptyList()), ConflictPolicy.PROMPT)

        assertEquals(PlanAction.Pull, entry.action)
    }

    @Test
    fun `execute emits Started, per-item phases, Done with summary counts`() = runBlocking {
        val v = vault("e4")
        val dav = FakeWebDav()
        val dev1 = SyncRepository(v, dav, "dev1")
        val dev2 = SyncRepository(v, dav, "dev2")
        val behind = save("B", "old")
        dev1.pushOne(behind)
        dev2.pushOne(behind.copy(files = listOf(FileEntry("save.dat", "new".toByteArray())), mtime = "2026-03-01T00:00:00Z"))
        val conflict = save("C", "mine")
        dev2.pushOne(conflict.copy(files = listOf(FileEntry("save.dat", "theirs".toByteArray()))))
        val plan = dev1.plan(listOf(save("A", "ahead"), behind, conflict, save("X", "x").copy(system = "megadrive")), ConflictPolicy.PROMPT)
        val restored = mutableListOf<Pair<String, String>>()

        val events = dev1.execute(plan, restore = { e, files ->
            restored += e.save.groupKey to String(files.single().content); true
        }).toList()

        assertEquals(SyncProgress.Started(4), events.first())
        val phases = events.filterIsInstance<SyncProgress.Item>().map { it.save.gameKey to it.phase }
        assertEquals(listOf("A" to SyncPhase.Pushing, "B" to SyncPhase.Pulling, "B" to SyncPhase.Restoring), phases)
        assertEquals(listOf("switch/B/main" to "new"), restored)
        val summary = (events.last() as SyncProgress.Done).summary
        assertEquals(SyncSummary(pushed = 1, pulled = 1, conflicts = 1, errors = 1, errorMessages = summary.errorMessages), summary)
        assertEquals(1, summary.errorMessages.size)
        assertEquals("Done — 1 uploaded, 1 downloaded, 1 conflict, 1 failed", summary.headline())
    }

    @Test
    fun `failed restore counts as an error and the run continues`() = runBlocking {
        val v = vault("e5")
        val dav = FakeWebDav()
        val dev1 = SyncRepository(v, dav, "dev1")
        val dev2 = SyncRepository(v, dav, "dev2")
        val b = save("B", "old"); dev1.pushOne(b)
        dev2.pushOne(b.copy(files = listOf(FileEntry("save.dat", "new".toByteArray())), mtime = "2026-03-01T00:00:00Z"))
        val plan = dev1.plan(listOf(b, save("A", "ahead")), ConflictPolicy.PROMPT)

        val done = dev1.execute(plan, restore = { _, _ -> false }).toList().last() as SyncProgress.Done

        assertEquals(1, done.summary.errors)
        assertEquals(1, done.summary.pushed)
    }

    @Test
    fun `cancel stops before the next item and marks the summary cancelled`() = runBlocking {
        val dev1 = SyncRepository(vault("e6"), FakeWebDav(), "dev1")
        val plan = dev1.plan(listOf(save("A", "1"), save("B", "2")), ConflictPolicy.PROMPT)
        val seen = AtomicInteger()

        // Flows are sequential: the Item for A is collected before B's cancel check runs.
        val events = dev1.execute(plan, restore = { _, _ -> true }, isCancelled = { seen.get() >= 1 })
            .onEach { if (it is SyncProgress.Item) seen.incrementAndGet() }
            .toList()

        val done = events.last() as SyncProgress.Done
        assertTrue(done.summary.cancelled)
        assertEquals(1, done.summary.pushed)
    }

    @Test
    fun `headline omits zero parts and handles empty runs`() {
        assertEquals("Done — nothing to sync", SyncSummary().headline())
        assertEquals("Done — 2 synced, 3 conflicts", SyncSummary(inSync = 2, conflicts = 3).headline())
        assertEquals("Cancelled — 1 uploaded", SyncSummary(pushed = 1, cancelled = true).headline())
    }
}
