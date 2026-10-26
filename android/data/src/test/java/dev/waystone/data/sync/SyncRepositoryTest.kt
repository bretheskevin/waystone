package dev.waystone.data.sync

import dev.waystone.data.snapshot.SnapshotStore
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.ConflictPolicy
import uniffi.waystone_mobile.DeviceHead
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.PlanAction
import uniffi.waystone_mobile.PushOutcome
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.decidePull
import uniffi.waystone_mobile.localHash
import uniffi.waystone_mobile.pushOne
import uniffi.waystone_mobile.readRemoteHeads
import uniffi.waystone_mobile.vaultInit

class SyncRepositoryTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private fun makeSave(content: ByteArray, mtime: String) = NormalizedSave(
        source = "jksv",
        system = "switch",
        gameKey = "TEST_GAME",
        displayName = "Test Game",
        titleId = "TEST_GAME",
        serial = null,
        romCrc = null,
        confidence = Confidence.STRONG,
        slot = "main",
        kind = "native",
        groupKey = "switch/TEST_GAME/main",
        portable = true,
        mtime = mtime,
        files = listOf(FileEntry("save.dat", content)),
    )

    private fun newVault(passphrase: String): Vault = vaultInit(passphrase).vault

    @Test
    fun `status reports InSync after push`() = runBlocking {
        val repo = SyncRepository(newVault("t1"), FakeWebDav(), "dev1")
        val save = makeSave("hello".toByteArray(), "2026-01-01T00:00:00Z")

        assertEquals(PushOutcome.PUSHED, repo.pushOne(save))
        assertEquals(localHash(save), repo.hashOf(save))
        assertEquals(SourceStatus.InSync, repo.planOne(save, ConflictPolicy.PROMPT).status)
    }

    @Test
    fun `restoreHash round-trips content through the real vault`() = runBlocking {
        val repo = SyncRepository(newVault("t2"), FakeWebDav(), "dev1")
        val save = makeSave("remote-content".toByteArray(), "2026-01-01T00:00:00Z")
        repo.pushOne(save)

        var files = emptyList<FileEntry>()
        assertTrue(repo.restoreHash(save, repo.hashOf(save)) { files = it; true })

        assertEquals(1, files.size)
        assertEquals("save.dat", files[0].path)
        assertArrayEquals("remote-content".toByteArray(), files[0].content)
    }

    @Test
    fun `conflict surfaces under Prompt when a foreign head exists`() = runBlocking {
        val vault = newVault("t3")
        val dav = FakeWebDav()
        val repo = SyncRepository(vault, dav, "dev1")
        // Never pushed locally; a foreign head sits on the remote with a different hash.
        val save = makeSave("local-content".toByteArray(), "2026-01-01T00:00:00Z")
        val foreignHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        val headJson =
            """{"device_id":"other","hash":"$foreignHash","mtime":"2026-01-02T00:00:00Z"}"""
        val base = listOf("switch", "TEST_GAME", "main").joinToString("/") { vault.pathSegment(it) }
        dav.put("$base/heads/other.json", vault.encryptHeads(headJson.toByteArray()))

        assertEquals(SourceStatus.Conflict, repo.planOne(save, ConflictPolicy.PROMPT).status)

        val entry = repo.conflictEntry(repo.planOne(save, ConflictPolicy.PROMPT))
        assertNotNull(entry)
        assertEquals(localHash(save), entry!!.localHash)
        assertEquals(foreignHash, entry.remoteHash)
        assertEquals("other", entry.remoteDeviceId)
        assertEquals("2026-01-02T00:00:00Z", entry.remoteMtime)
    }

    @Test
    fun `status reports Error when hashing fails`() = runBlocking {
        val repo = SyncRepository(newVault("t5"), FakeWebDav(), "dev1")
        val bad = makeSave("x".toByteArray(), "2026-01-01T00:00:00Z").copy(system = "megadrive")

        assertEquals(PlanAction.ERROR, repo.planOne(bad, ConflictPolicy.PROMPT).action)
    }

    @Test
    fun `guardedRestore aborts when safety snapshot fails`() = runBlocking {
        val repo = SyncRepository(newVault("t6"), FakeWebDav(), "dev1")
        val save = makeSave("old".toByteArray(), "2026-01-01T00:00:00Z")
        val store = SnapshotStore(File("/nonexistent_waystone_guard_test/backups"))
        val writes = mutableListOf<List<FileEntry>>()

        val ok = repo.guardedRestore(
            save,
            listOf(FileEntry("save.dat", "new".toByteArray())),
            write = { writes.add(it); it.size },
            currentLocalFiles = { listOf(FileEntry("save.dat", "old".toByteArray())) },
            takeSnapshot = store::snapshot,
            prune = { store.prune(it) },
            safetyBackup = true,
        )

        assertFalse(ok)
        assertTrue(writes.isEmpty())
    }

    @Test
    fun `guardedRestore proceeds without safety backup`() = runBlocking {
        val repo = SyncRepository(newVault("t7"), FakeWebDav(), "dev1")
        val save = makeSave("old".toByteArray(), "2026-01-01T00:00:00Z")
        val store = SnapshotStore(File("/nonexistent_waystone_guard_test/backups"))
        val writes = mutableListOf<List<FileEntry>>()

        val ok = repo.guardedRestore(
            save,
            listOf(FileEntry("save.dat", "new".toByteArray())),
            write = { writes.add(it); it.size },
            currentLocalFiles = { listOf(FileEntry("save.dat", "old".toByteArray())) },
            takeSnapshot = store::snapshot,
            prune = { store.prune(it) },
            safetyBackup = false,
        )

        assertTrue(ok)
        assertEquals(1, writes.size)
    }

    @Test
    fun `prune runs after successful guardedRestore`() = runBlocking {
        val repo = SyncRepository(newVault("t8"), FakeWebDav(), "dev1")
        val save = makeSave("old".toByteArray(), "2026-01-01T00:00:00Z")
        val root = tmp.newFolder("backups")
        val store = SnapshotStore(root)
        // 12 pre-existing valid ts dirs; the safety snapshot adds a 13th.
        val keyDir = File(root, "switch_TEST_GAME_main").apply { mkdirs() }
        repeat(12) { i -> File(keyDir, String.format("2026%02d01T000000Z", i + 1)).mkdir() }

        val ok = repo.guardedRestore(
            save,
            listOf(FileEntry("save.dat", "new".toByteArray())),
            write = { it.size },
            currentLocalFiles = { listOf(FileEntry("save.dat", "old".toByteArray())) },
            takeSnapshot = store::snapshot,
            prune = { store.prune(it) },
            safetyBackup = true,
        )

        assertTrue(ok)
        assertEquals(10, store.list("switch/TEST_GAME/main").size)
    }

    @Test
    fun `guardedRestore skips the snapshot when there is no local save`() = runBlocking {
        val repo = SyncRepository(newVault("t10"), FakeWebDav(), "dev1")
        val save = makeSave("x".toByteArray(), "2026-01-01T00:00:00Z")
        var snapshots = 0
        val ok = repo.guardedRestore(
            save, listOf(FileEntry("save.dat", "new".toByteArray())),
            write = { it.size }, currentLocalFiles = { emptyList() },
            takeSnapshot = { _, _ -> snapshots++; false }, prune = {}, safetyBackup = true,
        )
        assertTrue(ok)
        assertEquals(0, snapshots)
    }

    @Test
    fun `worst status fold picks highest rank`() {
        assertEquals(SourceStatus.Ahead, SourceStatus.worst(listOf(SourceStatus.InSync, SourceStatus.Ahead)))
        assertEquals(SourceStatus.Conflict, SourceStatus.worst(listOf(SourceStatus.Ahead, SourceStatus.Conflict)))
        assertEquals(SourceStatus.Error, SourceStatus.worst(listOf(SourceStatus.Conflict, SourceStatus.Error)))
        assertEquals(SourceStatus.InSync, SourceStatus.worst(listOf(SourceStatus.InSync, SourceStatus.InSync)))
    }
}
