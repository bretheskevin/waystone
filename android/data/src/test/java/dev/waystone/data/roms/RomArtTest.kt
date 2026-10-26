package dev.waystone.data.roms

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class RomArtTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun ndsRom(name: String): File {
        val rom = ByteArray(0x1000)
        val bannerOff = 0x400
        rom[0x68] = (bannerOff and 0xFF).toByte()
        rom[0x69] = (bannerOff shr 8).toByte()
        rom[bannerOff + 0x220] = 0xFF.toByte(); rom[bannerOff + 0x221] = 0x7F
        rom[bannerOff + 0x222] = 0x1F
        rom[bannerOff + 0x20] = 0x21
        return File(tmp.root, name).also { it.writeBytes(rom) }
    }

    @Test
    fun `nds icon decodes banner pixel 0 as red`() {
        ndsRom("Mario.nds")
        val icon = RomArtLoader.ndsIcon(DirRomFileSource(tmp.root), "Mario.nds")!!
        assertEquals(32, icon.width)
        assertArrayEquals(byteArrayOf(-1, 0, 0, -1), icon.pixels.copyOfRange(0, 4))
    }

    @Test
    fun `nds icon is null for a missing file or a ROM without banner`() {
        assertNull(RomArtLoader.ndsIcon(DirRomFileSource(tmp.root), "none.nds"))
        File(tmp.root, "x.nds").writeBytes(ByteArray(0x300))
        assertNull(RomArtLoader.ndsIcon(DirRomFileSource(tmp.root), "x.nds"))
    }

    @Test
    fun `box art prefers TWiLight then RetroArch and skips oversized files`() {
        val tw = File(tmp.root, "_nds/TWiLightMenu/boxart").apply { mkdirs() }
        val ra = File(tmp.root, "thumbnails/Nintendo - Game Boy Advance/Named_Boxarts").apply { mkdirs() }
        File(ra, "Ruby.png").writeBytes(byteArrayOf(1))
        val src = listOf<RomFileSource>(DirRomFileSource(tmp.root))
        assertArrayEquals(byteArrayOf(1), RomArtLoader.boxArt(src, "gba", "Ruby.gba")!!.bytes)

        File(tw, "Ruby.gba.png").writeBytes(byteArrayOf(2))
        assertArrayEquals(byteArrayOf(2), RomArtLoader.boxArt(src, "gba", "Ruby.gba")!!.bytes)

        File(tw, "Ruby.gba.png").writeBytes(ByteArray(RomArtLoader.MAX_ART_BYTES + 1))
        assertArrayEquals(byteArrayOf(1), RomArtLoader.boxArt(src, "gba", "Ruby.gba")!!.bytes)
    }

    @Test
    fun `no art found yields null and unknown systems only try TWiLight paths`() {
        assertNull(RomArtLoader.boxArt(listOf(DirRomFileSource(tmp.root)), "gba", "Nope.gba"))
        assertEquals(2, RomArtLoader.boxArtCandidates("bogus", "a.rom").size)
        assertNotNull(RomArtLoader.boxArtCandidates("gba", "a.gba").last())
    }
}
