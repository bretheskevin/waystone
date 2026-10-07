package dev.waystone.data.roms

import java.io.File
import java.util.zip.CRC32
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class RomScannerTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun write(rel: String, bytes: ByteArray) =
        File(tmp.root, rel).apply { parentFile.mkdirs(); writeBytes(bytes) }

    private fun ndsRom(): ByteArray = ByteArray(0x400).also {
        "AMCE".toByteArray().copyInto(it, 0x0C)
        it[0x15E] = 0x2B; it[0x15F] = 0x1A
    }

    @Test
    fun `pairs ROMs, identifies them and normalizes their saves`() {
        write("roms/nds/Mario.nds", ndsRom())
        write("roms/nds/saves/Mario.sav", "nds-save".toByteArray())
        val nes = ByteArray(300_000) { (it * 7).toByte() }
        write("roms/nes/Zelda.nes", nes)
        write("roms/nes/saves/Zelda.sav", "nes-save".toByteArray())
        write("roms/nes/NoSave.nes", byteArrayOf(1, 2, 3))

        val scanner = RomScanner(DirRomFileSource(tmp.root))
        val roms = scanner.scan()
        assertEquals(3, roms.size)
        val expectedCrc = "%08X".format(CRC32().apply { update(nes) }.value)
        assertEquals(expectedCrc, roms.first { it.romFileName == "Zelda.nes" }.romId)

        val keys = scanner.loadSaves(roms).map { it.groupKey }.sorted()
        assertEquals(listOf("nds/AMCE-1A2B/battery", "nes/$expectedCrc/battery"), keys)
    }

    @Test
    fun `ROM with an unreadable save file is skipped entirely`() {
        write("roms/nds/Mario.nds", ndsRom())
        write("roms/nds/saves/Mario.pub", "pub".toByteArray())
        write("roms/nds/saves/Mario.prv", "prv".toByteArray())
        val dir = DirRomFileSource(tmp.root)
        val flaky = object : RomFileSource {
            override fun listPaths(maxDepth: Int) = dir.listPaths(maxDepth)
            override fun open(path: String) = if (path.endsWith(".prv")) null else dir.open(path)
        }
        val scanner = RomScanner(flaky)
        assertEquals(emptyList<String>(), scanner.loadSaves(scanner.scan()).map { it.groupKey })
    }
}
