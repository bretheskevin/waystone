package dev.waystone.data.roms

import uniffi.waystone_mobile.FileEntry

enum class RestoreTree { Roms, Saves }

data class RestorePlacement(val tree: RestoreTree, val files: List<FileEntry>) {
    companion object {
        fun of(saveDir: String, native: List<FileEntry>): RestorePlacement {
            val external = CompositeRomFileSource.isExternal(saveDir)
            val dir = if (external) CompositeRomFileSource.stripExternal(saveDir) else saveDir
            val placed = native.map { f -> FileEntry(if (dir.isEmpty()) f.path else "$dir/${f.path}", f.content) }
            return RestorePlacement(if (external) RestoreTree.Saves else RestoreTree.Roms, placed)
        }
    }
}
