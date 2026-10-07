package dev.waystone.data.roms

import java.io.InputStream
import java.util.logging.Level
import java.util.logging.Logger
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.RawFileEntry
import uniffi.waystone_mobile.RawTree
import uniffi.waystone_mobile.RomPairing
import uniffi.waystone_mobile.crc32Update
import uniffi.waystone_mobile.romDisplayName
import uniffi.waystone_mobile.romIdentity
import uniffi.waystone_mobile.romKeyedNormalize
import uniffi.waystone_mobile.romNeedsFullHash
import uniffi.waystone_mobile.romPair

/** Read-only view of a ROM root; paths are root-relative with '/' separators. */
interface RomFileSource {
    fun listPaths(maxDepth: Int): List<String>
    fun open(path: String): InputStream?
}

data class LocalRom(
    val system: String,
    val romPath: String,
    val romFileName: String,
    val romId: String,
    val displayName: String,
    val saveDir: String,
    val savePaths: List<String>,
)

class RomScanner(private val source: RomFileSource) {

    private val log = Logger.getLogger(RomScanner::class.java.name)

    fun scan(): List<LocalRom> {
        val started = System.currentTimeMillis()
        val paths = source.listPaths(MAX_DEPTH)
        val pairings = romPair(paths)
        log.info("[roms] ${paths.size} file(s) -> ${pairings.size} ROM(s)")
        val roms = pairings.mapNotNull { p ->
            runCatching { identify(p) }
                .onFailure { log.log(Level.WARNING, "[roms] identity failed for ${p.romPath}", it) }
                .getOrNull()
        }
        log.info("[roms] scan done: ${roms.size}/${pairings.size} identified in ${System.currentTimeMillis() - started} ms")
        return roms
    }

    // A ROM whose saves cannot all be read is skipped: a partial set (e.g. .pub without .prv) would push a bogus save.
    fun loadSaves(roms: List<LocalRom>): List<NormalizedSave> = roms.filter { it.savePaths.isNotEmpty() }.flatMap { rom ->
        val files = rom.savePaths.map { path ->
            val bytes = runCatching { source.open(path)?.use { it.readBytes() } }
                .onFailure { log.log(Level.WARNING, "[roms] read failed for save $path", it) }
                .getOrNull()
            if (bytes == null) {
                log.warning("[roms] cannot read save $path; skipping ${rom.romPath}")
                return@flatMap emptyList()
            }
            RawFileEntry(path.substringAfterLast('/'), bytes)
        }
        romKeyedNormalize(rom.system, rom.romId, rom.displayName, rom.romFileName, RawTree(files))
            .also { log.info("[roms] ${rom.romPath}: ${it.size} save(s) from ${files.size} file(s)") }
    }

    private fun identify(p: RomPairing): LocalRom {
        val header = source.open(p.romPath)?.use { readUpTo(it, HEADER_LEN) }
            ?: error("cannot open ${p.romPath}")
        val crc: UInt? = if (romNeedsFullHash(p.system, header)) {
            source.open(p.romPath)?.use { input ->
                var c = 0u
                var total = 0L
                val buf = ByteArray(CHUNK)
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    if (n == 0) continue
                    c = crc32Update(c, if (n == buf.size) buf else buf.copyOf(n))
                    total += n
                }
                log.info("[roms] hashed ${p.romPath}: $total bytes")
                c
            } ?: error("cannot reopen ${p.romPath}")
        } else null
        val id = romIdentity(p.system, header, crc) ?: error("no identity for ${p.romPath}")
        val name = p.romPath.substringAfterLast('/')
        return LocalRom(p.system, p.romPath, name, id, romDisplayName(p.system, header, name), p.saveDir, p.savePaths)
    }

    companion object {
        const val MAX_DEPTH = 6
        private const val HEADER_LEN = 0x200
        private const val CHUNK = 64 * 1024

        private fun readUpTo(input: InputStream, n: Int): ByteArray {
            val out = ByteArray(n)
            var off = 0
            while (off < n) {
                val r = input.read(out, off, n - off)
                if (r < 0) break
                off += r
            }
            return out.copyOf(off)
        }
    }
}
