package dev.waystone.data.fs

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import dev.waystone.data.roms.RomFileSource
import java.io.InputStream
import java.util.logging.Level
import java.util.logging.Logger

class SafRomFileSource(private val context: Context, private val treeUri: Uri) : RomFileSource {

    private val log = Logger.getLogger(SafRomFileSource::class.java.name)
    private val docs = HashMap<String, DocumentFile>()

    override fun listPaths(maxDepth: Int): List<String> {
        docs.clear()
        val root = DocumentFile.fromTreeUri(context, treeUri)
            ?: throw IllegalStateException("cannot open SAF tree: $treeUri")
        walk(root, "", 0, maxDepth)
        log.info("[roms] SAF listed ${docs.size} file(s) under $treeUri")
        return docs.keys.sorted()
    }

    private fun walk(dir: DocumentFile, prefix: String, depth: Int, maxDepth: Int) {
        for (child in dir.listFiles()) {
            val name = child.name ?: continue
            val rel = if (prefix.isEmpty()) name else "$prefix/$name"
            if (child.isDirectory) {
                if (depth < maxDepth) walk(child, rel, depth + 1, maxDepth)
            } else if (child.isFile) {
                docs[rel] = child
            }
        }
    }

    override fun open(path: String): InputStream? {
        val doc = docs[path] ?: run {
            log.warning("[roms] SAF open: unknown path $path")
            return null
        }
        return try {
            context.contentResolver.openInputStream(doc.uri)
        } catch (e: Exception) {
            log.log(Level.WARNING, "[roms] SAF open failed for $path", e)
            null
        }
    }
}
