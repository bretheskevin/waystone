package dev.waystone.data.snapshot

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SnapshotStoreTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private val tsRegex = Regex("""\d{8}T\d{6}Z""")

    private fun newStore(root: File = tmp.newFolder("backups")) = SnapshotStore(root)

    @Test
    fun `sanitizeKey blocks traversal and maps separators`() {
        assertEquals("______etc_passwd", SnapshotStore.sanitizeKey("../../../etc/passwd"))
        assertEquals("3ds_GAME_slot", SnapshotStore.sanitizeKey("""3ds\GAME\slot"""))
        assertEquals("switch_JKSV", SnapshotStore.sanitizeKey("switch/JKSV"))
        assertEquals("plain_key", SnapshotStore.sanitizeKey("plain_key"))
    }

    @Test
    fun `timestamp formats 16-char UTC ts dir names`() {
        val ts = SnapshotStore.timestamp(java.time.Instant.parse("2026-09-21T13:05:56Z"))
        assertEquals(16, ts.length)
        assertTrue(tsRegex.matches(ts))
        assertEquals("20260921T130556Z", ts)
    }

    @Test
    fun `isTsDirName is strict`() {
        assertTrue(SnapshotStore.isTsDirName("20260921T130556Z"))
        assertFalse(SnapshotStore.isTsDirName("not-a-ts-dir"))
        assertFalse(SnapshotStore.isTsDirName("20260921T130556"))      // 15 chars
        assertFalse(SnapshotStore.isTsDirName("2026092xT130556Z"))     // non-digit
        assertFalse(SnapshotStore.isTsDirName("202609211130556Z"))     // T missing
        assertFalse(SnapshotStore.isTsDirName("20260921T130556x"))     // Z missing
    }

    @Test
    fun `snapshot writes files under sanitized key in a ts dir`() {
        val store = newStore()
        assertTrue(store.snapshot("3ds/GAME", mapOf("save1/data.bin" to byteArrayOf(1, 2, 3))))

        val keyDir = File(tmp.root, "backups/3ds_GAME")
        val tsDirs = keyDir.listFiles { f -> f.isDirectory }!!.toList()
        assertEquals(1, tsDirs.size)
        assertTrue(tsRegex.matches(tsDirs[0].name))
        assertArrayEquals(byteArrayOf(1, 2, 3), File(tsDirs[0], "save1/data.bin").readBytes())
    }

    @Test
    fun `snapshot returns false on failure and logs`() {
        val store = newStore()
        // Empty relative path is rejected, mirroring shell write_snapshot semantics.
        assertFalse(store.snapshot("k", mapOf("" to byteArrayOf(1))))
    }

    @Test
    fun `list returns ts dirs newest-first only`() {
        val store = newStore()
        val keyDir = File(tmp.root, "backups/switch_JKSV").apply { mkdirs() }
        File(keyDir, "20250101T000001Z").mkdir()
        File(keyDir, "20260101T000000Z").mkdir()
        File(keyDir, "20251231T235959Z").mkdir()
        File(keyDir, "not-a-ts-dir").mkdir()

        val names = store.list("switch/JKSV").map { it.name }
        assertEquals(listOf("20260101T000000Z", "20251231T235959Z", "20250101T000001Z"), names)
    }

    @Test
    fun `prune keeps 10 newest and never deletes non-ts dirs`() {
        val store = newStore()
        val keyDir = File(tmp.root, "backups/switch_JKSV").apply { mkdirs() }
        val tss = (1..13).map { String.format("2026%02d01T000000Z", it) }
        tss.forEach { File(keyDir, it).mkdir() }
        File(keyDir, "not-a-ts-dir").mkdir()
        // Foreign key's valid-looking ts dir must be untouched.
        val foreign = File(tmp.root, "backups/other_key/20200101T000000Z").apply { mkdirs() }

        store.prune("switch/JKSV")

        val remaining = keyDir.listFiles { f -> f.isDirectory }!!.map { it.name }.toSet()
        assertEquals(setOf("not-a-ts-dir") + tss.takeLast(10).toSet(), remaining)
        assertTrue(foreign.isDirectory)
    }

    @Test
    fun `prune delete failure is non-fatal`() {
        val store = newStore()
        val keyDir = File(tmp.root, "backups/k").apply { mkdirs() }
        val victim = File(keyDir, "20260101T000000Z").apply { mkdir() }
        File(victim, "inner.txt").writeBytes(byteArrayOf(1))
        File(keyDir, "20260201T000000Z").mkdir()
        // Make victim's contents undeletable on POSIX (dir not readable/writable).
        assertTrue(victim.setReadable(false, false))
        assertTrue(victim.setWritable(false, false))
        assertTrue(victim.setExecutable(false, false))

        store.prune("k", keep = 1)

        assertTrue(victim.isDirectory) // delete failed, but prune did not throw
        assertTrue(File(keyDir, "20260201T000000Z").isDirectory)
        victim.setReadable(true, false)
        victim.setWritable(true, false)
        victim.setExecutable(true, false)
    }

    @Test
    fun `restore overwrite-merges into dest`() {
        val store = newStore()
        val dest = tmp.newFolder("dest")
        File(dest, "a.sav").writeBytes(byteArrayOf(7))   // pre-existing, must be overwritten
        File(dest, "keepme.sav").writeBytes(byteArrayOf(9)) // not in snapshot, must survive

        val keyDir = File(tmp.root, "backups/switch_JKSV/20260101T000000Z").apply { mkdirs() }
        File(keyDir, "a.sav").writeBytes(byteArrayOf(1))
        File(keyDir, "sub").mkdirs()
        File(keyDir, "sub/b.sav").writeBytes(byteArrayOf(2))

        store.restore("switch/JKSV", "20260101T000000Z", dest)

        assertArrayEquals(byteArrayOf(1), File(dest, "a.sav").readBytes())
        assertArrayEquals(byteArrayOf(2), File(dest, "sub/b.sav").readBytes())
        assertArrayEquals(byteArrayOf(9), File(dest, "keepme.sav").readBytes())
    }
}
