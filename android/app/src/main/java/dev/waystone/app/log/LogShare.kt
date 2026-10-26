package dev.waystone.app.log

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import dev.waystone.data.log.RotatingLogFile
import dev.waystone.data.log.WsLogFile
import java.io.File

object LogShare {
    fun logDir(context: Context) = File(context.filesDir, "logs")

    /** Live log first, then the rotated one; missing or empty files are skipped. */
    fun filesToShare(dir: File): List<File> =
        listOf(WsLogFile.FILE_NAME, WsLogFile.FILE_NAME + RotatingLogFile.OLD_SUFFIX)
            .map { File(dir, it) }
            .filter { it.isFile && it.length() > 0 }

    fun intent(context: Context, files: List<File>): Intent? {
        if (files.isEmpty()) return null
        val authority = "${context.packageName}.logs"
        val uris = ArrayList<Uri>(files.map { FileProvider.getUriForFile(context, authority, it) })
        val send = if (uris.size == 1) {
            Intent(Intent.ACTION_SEND).putExtra(Intent.EXTRA_STREAM, uris[0])
        } else {
            Intent(Intent.ACTION_SEND_MULTIPLE).putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris)
        }
        send.setType("text/plain").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        return Intent.createChooser(send, "Share Waystone logs").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
}
