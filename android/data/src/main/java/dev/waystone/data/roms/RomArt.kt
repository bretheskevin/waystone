package dev.waystone.data.roms

import java.io.InputStream
import uniffi.waystone_mobile.ndsBannerIconRgba
import uniffi.waystone_mobile.ndsBannerLen
import uniffi.waystone_mobile.ndsBannerOffset
import uniffi.waystone_mobile.ndsIconSide
import uniffi.waystone_mobile.retroarchBoxartPath
import uniffi.waystone_mobile.twilightBoxartPaths

sealed interface RomArt {
    /** Core-decoded RGBA8888 NDS banner icon. */
    class Rgba(val width: Int, val height: Int, val pixels: ByteArray) : RomArt

    /** PNG/BMP bytes, decoded by the UI layer. */
    class Encoded(val bytes: ByteArray) : RomArt
}

object RomArtLoader {
    const val MAX_ART_BYTES = 2 * 1024 * 1024

    fun ndsIcon(source: RomFileSource, romPath: String): RomArt.Rgba? {
        val header = source.open(romPath)?.use { it.readUpTo(ROM_HEADER_LEN) } ?: return null
        val off = ndsBannerOffset(header)?.toLong() ?: return null
        val banner = source.open(romPath)?.use { s ->
            s.skipFully(off)
            s.readUpTo(ndsBannerLen().toInt())
        } ?: return null
        val px = ndsBannerIconRgba(banner) ?: return null
        val side = ndsIconSide().toInt()
        return RomArt.Rgba(side, side, px)
    }

    /** Candidates relative to a granted tree root: TWiLight first, then the RetroArch thumbnail. */
    fun boxArtCandidates(system: String, romFileName: String): List<String> =
        twilightBoxartPaths(romFileName) +
            listOfNotNull(runCatching { retroarchBoxartPath(system, romFileName) }.getOrNull())

    fun boxArt(sources: List<RomFileSource>, system: String, romFileName: String): RomArt.Encoded? {
        for (path in boxArtCandidates(system, romFileName)) {
            for (source in sources) {
                val bytes = runCatching { source.open(path)?.use { it.readUpTo(MAX_ART_BYTES + 1) } }.getOrNull()
                if (bytes != null && bytes.isNotEmpty() && bytes.size <= MAX_ART_BYTES) return RomArt.Encoded(bytes)
            }
        }
        return null
    }

    private fun InputStream.skipFully(n: Long) {
        var left = n
        while (left > 0) {
            val s = skip(left)
            if (s <= 0) {
                if (read() < 0) return
                left--
            } else {
                left -= s
            }
        }
    }
}
