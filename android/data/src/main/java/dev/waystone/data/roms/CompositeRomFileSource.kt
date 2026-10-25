package dev.waystone.data.roms

import java.io.InputStream

/** ROM tree + an external save tree exposed under the core's `@saves/` prefix (depth 0 and 1). */
class CompositeRomFileSource(private val roms: RomFileSource, private val saves: RomFileSource) : RomFileSource {

    override fun listPaths(maxDepth: Int): List<String> =
        roms.listPaths(maxDepth) + saves.listPaths(EXTERNAL_DEPTH).map { "$PREFIX/$it" }

    override fun open(path: String): InputStream? =
        if (isExternal(path)) saves.open(stripExternal(path)) else roms.open(path)

    companion object {
        const val PREFIX = "@saves"
        const val EXTERNAL_DEPTH = 1

        fun isExternal(path: String): Boolean = path == PREFIX || path.startsWith("$PREFIX/")

        fun stripExternal(path: String): String = path.removePrefix(PREFIX).trimStart('/')
    }
}
