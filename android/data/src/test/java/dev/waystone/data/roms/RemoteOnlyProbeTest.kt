package dev.waystone.data.roms

import dev.waystone.data.sync.FakeWebDav
import dev.waystone.data.sync.SyncRepository
import dev.waystone.data.sync.isRemoteOnly
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.vaultInit

class RemoteOnlyProbeTest {
    private fun rom(system: String, id: String, name: String) =
        LocalRom(system, "roms/$system/$name", name, id, name.substringBeforeLast('.'), "roms/$system/saves", emptyList())

    private fun remoteSave(system: String, id: String, slot: String) = NormalizedSave(
        source = "rom_keyed", system = system, gameKey = id, displayName = id, titleId = null, serial = null,
        romCrc = null, confidence = Confidence.STRONG, slot = slot, kind = "battery",
        groupKey = "$system/$id/$slot", portable = true, mtime = "2026-01-01T00:00:00Z",
        files = listOf(FileEntry(slot, byteArrayOf(1, 2, 3))),
    )

    @Test
    fun `finds remote-only slots with one system PROPFIND and skips local or unknown ones`() = runBlocking {
        val vault = vaultInit("p1").vault
        val dav = FakeWebDav()
        val other = SyncRepository(vault, dav, "dev2")
        other.pushOne(remoteSave("gba", "AXVE-01", "battery"))
        other.pushOne(remoteSave("nds", "AMCE-1A2B", "slot-2"))
        other.pushOne(remoteSave("nds", "AMCE-1A2B", "battery"))
        val roms = listOf(
            rom("gba", "AXVE-01", "Ruby.gba"),
            rom("gba", "NOPE-00", "Other.gba"),
            rom("nds", "AMCE-1A2B", "Mario.nds"),
        )
        dav.propfinds.clear()

        val found = RemoteOnlyProbe(vault, dav).find(roms, localGroupKeys = setOf("nds/AMCE-1A2B/battery"))

        assertEquals(listOf("gba/AXVE-01/battery", "nds/AMCE-1A2B/slot-2"), found.map { it.groupKey })
        assertTrue(found.all { it.isRemoteOnly && it.source == "rom_keyed" })
        assertEquals("Ruby", found[0].displayName)
        assertEquals(2, dav.propfinds.count { !it.contains('/') }) // one per system dir
    }

    @Test
    fun `non-NDS ROM whose only slot is local is never probed`() = runBlocking {
        val vault = vaultInit("p2").vault
        val dav = FakeWebDav()
        val found = RemoteOnlyProbe(vault, dav).find(listOf(rom("gba", "A-1", "A.gba")), setOf("gba/A-1/battery"))
        assertTrue(found.isEmpty())
        assertTrue(dav.propfinds.isEmpty())
    }
}
