package dev.waystone.data.roms

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import uniffi.waystone_mobile.localHash

class FlavoredScanTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun write(root: File, rel: String, bytes: ByteArray) =
        File(root, rel).apply { parentFile.mkdirs(); writeBytes(bytes) }

    private fun ndsRom() = ByteArray(0x400).also {
        "AMCE".toByteArray().copyInto(it, 0x0C); it[0x15E] = 0x2B; it[0x15F] = 0x1A
    }

    @Test
    fun `retroarch per-core srm pairs through the composite source`() {
        val roms = tmp.newFolder("roms"); val saves = tmp.newFolder("saves")
        write(roms, "gba/Zelda.gba", ByteArray(0x200))
        write(saves, "mGBA/Zelda.srm", "gba-save".toByteArray())
        val scanner = RomScanner(CompositeRomFileSource(DirRomFileSource(roms), DirRomFileSource(saves)), "retroarch")
        val rom = scanner.scan().single()
        assertEquals("@saves/mGBA", rom.saveDir)
        assertEquals("retroarch", rom.flavor)
        val save = scanner.loadSaves(listOf(rom)).single()
        assertArrayEquals("gba-save".toByteArray(), save.files.single().content)
    }

    @Test
    fun `drastic dsv and twilight sav converge on one hash`() {
        val raw = ByteArray(8192) { 0x42 }
        val seed = tmp.newFolder("seed").also { write(it, "M.nds", ndsRom()); write(it, "M.sav", raw) }
        val seedScanner = RomScanner(DirRomFileSource(seed))
        val dsv = uniffi.waystone_mobile.romKeyedToNative(
            seedScanner.loadSaves(seedScanner.scan()).single(),
            "M.nds", "drastic", emptyList(),
        ).files.single().content
        val a = tmp.newFolder("a"); val aSaves = tmp.newFolder("a-saves")
        write(a, "Mario.nds", ndsRom()); write(aSaves, "Mario.dsv", dsv)
        val b = tmp.newFolder("b")
        write(b, "mkds.nds", ndsRom()); write(b, "mkds.sav", raw)
        val sa = RomScanner(CompositeRomFileSource(DirRomFileSource(a), DirRomFileSource(aSaves)), "drastic").let { it.loadSaves(it.scan()) }.single()
        val sb = RomScanner(DirRomFileSource(b)).let { it.loadSaves(it.scan()) }.single()
        assertEquals(sb.groupKey, sa.groupKey)
        assertEquals(localHash(sb), localHash(sa))
    }
}
