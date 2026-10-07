package dev.waystone.data.roms

import java.io.File
import java.io.InputStream

class DirRomFileSource(private val root: File) : RomFileSource {
    override fun listPaths(maxDepth: Int): List<String> =
        root.walkTopDown().maxDepth(maxDepth + 1).filter { it.isFile }
            .map { it.relativeTo(root).invariantSeparatorsPath }.sorted().toList()

    override fun open(path: String): InputStream? = File(root, path).takeIf { it.isFile }?.inputStream()
}
