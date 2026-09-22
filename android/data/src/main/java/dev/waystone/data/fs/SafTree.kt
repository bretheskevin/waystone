package dev.waystone.data.fs

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import java.util.logging.Level
import java.util.logging.Logger
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.RawFileEntry
import uniffi.waystone_mobile.RawTree

/**
 * SAF tree <-> RawTree bridge (mirrors desktop helpers::read_source_tree on
 * the read side; path-safety rules mirror write-time traversal guards).
 */
class SafTree(private val context: Context, private val treeUri: Uri) {

    private val log = Logger.getLogger(SafTree::class.java.name)

    fun toRawTree(): RawTree {
        val root = DocumentFile.fromTreeUri(context, treeUri)
            ?: throw IllegalStateException("cannot open SAF tree: $treeUri")
        val files = mutableListOf<RawFileEntry>()
        walk(root, "", files)
        log.info("SAF tree read: ${files.size} files from $treeUri")
        return RawTree(files)
    }

    private fun walk(dir: DocumentFile, prefix: String, out: MutableList<RawFileEntry>) {
        for (child in dir.listFiles()) {
            val name = child.name
            if (name == null) {
                log.warning("skipping entry with null name under $prefix")
                continue
            }
            val rel = if (prefix.isEmpty()) name else "$prefix/$name"
            if (child.isDirectory) {
                walk(child, rel, out)
            } else if (child.isFile) {
                val uri = child.uri
                try {
                    context.contentResolver.openInputStream(uri)?.use { input ->
                        out.add(RawFileEntry(rel, input.readBytes()))
                    } ?: log.warning("openInputStream returned null for $rel")
                } catch (e: Exception) {
                    log.log(Level.WARNING, "read failed for $rel ($uri)", e)
                }
            }
        }
    }

    /** Writes files into the tree, creating directories as needed. Returns the number written. */
    fun writeFiles(files: List<FileEntry>): Int {
        val root = DocumentFile.fromTreeUri(context, treeUri)
            ?: throw IllegalStateException("cannot open SAF tree: $treeUri")
        var written = 0
        for (file in files) {
            val rel = normalizeRelativePath(file.path)
            if (rel == null) {
                log.warning("skipping unsafe path: ${file.path}")
                continue
            }
            try {
                val target = resolveOrCreate(root, rel)
                if (target == null) {
                    log.warning("could not create $rel in $treeUri")
                    continue
                }
                context.contentResolver.openOutputStream(target.uri, "wt")?.use { output ->
                    output.write(file.content)
                } ?: log.warning("openOutputStream returned null for $rel")
                written++
                log.info("wrote $rel (${file.content.size}B)")
            } catch (e: Exception) {
                log.log(Level.WARNING, "write failed for $rel", e)
            }
        }
        return written
    }

    private fun resolveOrCreate(root: DocumentFile, relativePath: String): DocumentFile? {
        val segments = relativePath.split('/').filter { it.isNotEmpty() }
        if (segments.isEmpty()) return null
        var current = root
        for ((index, segment) in segments.withIndex()) {
            val last = index == segments.size - 1
            if (last) {
                val existing = current.findFile(segment)
                if (existing?.isFile == true) return existing
                return current.createFile("application/octet-stream", segment)
            }
            val dir = current.findFile(segment)?.takeIf { it.isDirectory }
                ?: current.createDirectory(segment)
                ?: return null
            current = dir
        }
        return null
    }

    companion object {
        fun isSafeRelativePath(path: String): Boolean = normalizeRelativePath(path) != null

        /** Trims leading separators; null when the path is empty or contains a `..` segment. */
        fun normalizeRelativePath(path: String): String? {
            val trimmed = path.trimStart('/', '\\')
            if (trimmed.isEmpty()) return null
            val segments = trimmed.split('/', '\\')
            if (segments.any { it == ".." }) return null
            return trimmed
        }
    }
}
