package dev.waystone.data.snapshot

import java.io.File
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.logging.Level
import java.util.logging.Logger

class SnapshotStore(private val root: File) {

    private val log = Logger.getLogger(SnapshotStore::class.java.name)

    fun snapshot(key: String, files: Map<String, ByteArray>): Boolean {
        val keyDir = File(root, sanitizeKey(key))
        val dir = File(keyDir, timestamp(Instant.now()))
        return try {
            for ((rel, data) in files) {
                if (rel.isEmpty()) {
                    log.warning("snapshot $key aborted: empty file path")
                    return false
                }
                val f = File(dir, rel)
                f.parentFile?.mkdirs()
                f.writeBytes(data)
            }
            log.info("snapshot written: ${dir.path} (${files.size} files)")
            true
        } catch (e: Exception) {
            log.log(Level.WARNING, "snapshot failed for key=$key", e)
            false
        }
    }

    fun list(key: String): List<File> {
        val keyDir = File(root, sanitizeKey(key))
        val dirs = keyDir.listFiles { f -> f.isDirectory && isTsDirName(f.name) } ?: emptyArray()
        return dirs.sortedByDescending { it.name }
    }

    fun prune(key: String, keep: Int = KEEP) {
        val k = maxOf(keep, 1)
        val dirs = list(key).sortedBy { it.name }
        for (d in dirs.dropLast(k)) {
            if (d.deleteRecursively()) {
                log.info("pruned ${d.path}")
            } else {
                log.log(Level.WARNING, "prune delete failed (non-fatal): ${d.path}")
            }
        }
    }

    fun restore(key: String, tsDirName: String, dest: File): Boolean {
        val src = File(File(root, sanitizeKey(key)), tsDirName)
        return try {
            copyTree(src, dest)
            log.info("restored ${src.path} -> ${dest.path}")
            true
        } catch (e: Exception) {
            log.log(Level.WARNING, "restore failed: ${src.path} -> ${dest.path}", e)
            false
        }
    }

    companion object {
        const val KEEP = 10

        fun sanitizeKey(key: String): String =
            key.replace("..", "_").replace('/', '_').replace('\\', '_')

        fun timestamp(instant: Instant): String =
            DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC).format(instant)

        fun isTsDirName(name: String): Boolean =
            name.length == 16 && name[8] == 'T' && name[15] == 'Z' &&
                name.substring(0, 8).all { it.isDigit() } &&
                name.substring(9, 15).all { it.isDigit() }
    }
}

private fun copyTree(srcRoot: File, dstRoot: File) {
    val files = srcRoot.walkTopDown().filter { it.isFile }.toList()
    for (f in files) {
        val rel = f.relativeTo(srcRoot).path
        val dest = File(dstRoot, rel)
        dest.parentFile?.mkdirs()
        f.copyTo(dest, overwrite = true)
    }
}
