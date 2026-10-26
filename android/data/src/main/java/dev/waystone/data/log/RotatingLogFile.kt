package dev.waystone.data.log

import java.io.File

/** Mirrors shell-common log_rotate: past [maxBytes] the file moves to `<path>.old` (previous .old discarded). */
class RotatingLogFile(private val file: File, private val maxBytes: Long = MAX_BYTES) {
    init {
        file.parentFile?.mkdirs()
        rotateIfNeeded()
    }

    @Synchronized
    fun append(line: String) {
        rotateIfNeeded()
        file.appendText(line + "\n")
    }

    private fun rotateIfNeeded() {
        if (!file.exists() || file.length() <= maxBytes) return
        val old = File(file.path + OLD_SUFFIX)
        old.delete()
        if (!file.renameTo(old)) file.delete()
    }

    companion object {
        const val MAX_BYTES = 256L * 1024
        const val OLD_SUFFIX = ".old"
    }
}
