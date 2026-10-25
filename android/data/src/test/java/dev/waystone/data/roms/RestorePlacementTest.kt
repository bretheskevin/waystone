package dev.waystone.data.roms

import org.junit.Assert.assertEquals
import org.junit.Test
import uniffi.waystone_mobile.FileEntry

class RestorePlacementTest {
    private val f = listOf(FileEntry("Zelda.srm", byteArrayOf(1)))

    @Test fun `external dirs map into the save tree`() {
        val p = RestorePlacement.of("@saves/mGBA", f)
        assertEquals(RestoreTree.Saves, p.tree)
        assertEquals("mGBA/Zelda.srm", p.files.single().path)
        assertEquals("Zelda.srm", RestorePlacement.of("@saves", f).files.single().path)
    }

    @Test fun `rom-tree dirs keep their relative path`() {
        val p = RestorePlacement.of("roms/gba/saves", f)
        assertEquals(RestoreTree.Roms, p.tree)
        assertEquals("roms/gba/saves/Zelda.srm", p.files.single().path)
        assertEquals("Zelda.srm", RestorePlacement.of("", f).files.single().path)
    }
}
