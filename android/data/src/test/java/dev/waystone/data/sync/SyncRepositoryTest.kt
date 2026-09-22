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
import uniffi.waystone_mobile.PushOutcome
import uniffi.waystone_mobile.SyncDecision
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.decidePull
import uniffi.waystone_mobile.localHash
import uniffi.waystone_mobile.pushOne
import uniffi.waystone_mobile.readRemoteHeads
import uniffi.waystone_mobile.vaultInit

class SyncRepositoryTest {

    @get:Rule
    val tmp = TemporaryFolder()

    /** Mirrors mobile/tests/mock_sync.rs::MockWebDav over an in-memory map. */
    class FakeWebDav : WebDav {
        private val store = mutableMapOf<String, ByteArray>()

        fun keys(): Set<String> = store.keys.toSet()

        override fun `get`(path: String): ByteArray? = store[path]?.copyOf()

        override fun `put`(path: String, body: ByteArray) {
            store[path] = body.copyOf()
        }

        override fun `exists`(path: String): Boolean = store.containsKey(path)

        override fun `propfind`(path: String): List<String> {
            val prefix = if (path.endsWith("/")) path else "$path/"
            return store.keys
                .filter { it.startsWith(prefix) && it.substring(prefix.length).indexOf('/') < 0 }
                .sorted()
        }

        override fun `mkdirP`(path: String) = Unit
    }

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
        assertEquals(SourceStatus.InSync, repo.statusOf(save))
    }

    @Test
    fun `keepRemoteFiles round-trips content through the real vault`() = runBlocking {
        val repo = SyncRepository(newVault("t2"), FakeWebDav(), "dev1")
        val save = makeSave("remote-content".toByteArray(), "2026-01-01T00:00:00Z")
        repo.pushOne(save)

        val files = repo.keepRemoteFiles(save, repo.hashOf(save))

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

        assertEquals(SourceStatus.Conflict, repo.statusOf(save))

        val decision = decidePull(
            localHash(save), save.mtime, readRemoteHeads(vault, save, dav), "dev1",
            ConflictPolicy.PROMPT,
        )
        val entry = repo.conflictEntry(
            save, readRemoteHeads(vault, save, dav),
            decision as SyncDecision.ConflictNeedsInput,
        )
        assertNotNull(entry)
        assertEquals(localHash(save), entry!!.localHash)
        assertEquals(foreignHash, entry.remoteHash)
        assertEquals("other", entry.remoteDeviceId)
        assertEquals("2026-01-02T00:00:00Z", entry.remoteMtime)
    }

    @Test
    fun `syncAll pushes when ahead`() = runBlocking {
        val vault = newVault("t4")
        val dav = FakeWebDav()
        val repo = SyncRepository(vault, dav, "dev1")
        val save = makeSave("ahead-content".toByteArray(), "2026-01-01T00:00:00Z")

        val results = repo.syncAll(listOf(save), ConflictPolicy.PROMPT)

        assertEquals(1, results.size)
        assertEquals(SyncDecision.Push, results[0].decision)
        val heads = readRemoteHeads(vault, save, dav)
        assertEquals(1, heads.size)
        assertEquals("dev1", heads[0].deviceId)
        assertEquals(localHash(save), heads[0].hash)
    }

    @Test
    fun `syncAll pulls remote files when behind`() = runBlocking {
        val vault = newVault("t9")
        val dav = FakeWebDav()
        val dev1 = SyncRepository(vault, dav, "dev1")
        val saveA = makeSave("content-a".toByteArray(), "2026-01-02T00:00:00Z")
        dev1.pushOne(saveA)
        // Another device pushes a newer save; dev1's local copy is now behind.
        val dev2 = SyncRepository(vault, dav, "dev2")
        val saveB = saveA.copy(
            files = listOf(FileEntry("save.dat", "content-b".toByteArray())),
            mtime = "2026-01-03T00:00:00Z",
        )
        dev2.pushOne(saveB)

        val results = dev1.syncAll(listOf(saveA), ConflictPolicy.PROMPT)

        assertEquals(1, results.size)
        assertTrue(results[0].decision is SyncDecision.Pull)
        val files = results[0].files
        assertNotNull(files)
        assertEquals("save.dat", files!![0].path)
        assertArrayEquals("content-b".toByteArray(), files[0].content)
    }

    @Test
    fun `status reports Error when hashing fails`() = runBlocking {
        val repo = SyncRepository(newVault("t5"), FakeWebDav(), "dev1")
        val bad = makeSave("x".toByteArray(), "2026-01-01T00:00:00Z").copy(system = "megadrive")

        assertEquals(SourceStatus.Error, repo.statusOf(bad))
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
    fun `worst status fold picks highest rank`() {
        assertEquals(SourceStatus.Ahead, SourceStatus.worst(listOf(SourceStatus.InSync, SourceStatus.Ahead)))
        assertEquals(SourceStatus.Conflict, SourceStatus.worst(listOf(SourceStatus.Ahead, SourceStatus.Conflict)))
        assertEquals(SourceStatus.Error, SourceStatus.worst(listOf(SourceStatus.Conflict, SourceStatus.Error)))
        assertEquals(SourceStatus.InSync, SourceStatus.worst(listOf(SourceStatus.InSync, SourceStatus.InSync)))
    }
}
