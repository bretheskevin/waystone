package dev.waystone.data.log

import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.logging.Handler
import java.util.logging.Level
import java.util.logging.LogRecord
import java.util.logging.Logger

/** File-only sink for every `WS:*` log line (android.util.Log via the app facade, JUL via [JulFileHandler]). */
object WsLogFile {
    const val FILE_NAME = "waystone.log"

    @Volatile
    private var sink: RotatingLogFile? = null
    private var handler: JulFileHandler? = null

    @Synchronized
    fun install(dir: File): File {
        val file = File(dir, FILE_NAME)
        val rotating = RotatingLogFile(file)
        sink = rotating
        handler?.let { Logger.getLogger("").removeHandler(it) }
        handler = JulFileHandler(rotating).also { Logger.getLogger("").addHandler(it) }
        return file
    }

    fun write(level: Char, tag: String, msg: String, tr: Throwable? = null) {
        runCatching { sink?.append(format(level, tag, msg, tr)) }
    }

    private val TS = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'").withZone(ZoneOffset.UTC)

    internal fun format(level: Char, tag: String, msg: String, tr: Throwable?): String {
        val trace = tr?.let { StringWriter().also { w -> it.printStackTrace(PrintWriter(w)) }.toString().trimEnd() }
        return "${TS.format(Instant.now())} $level $tag $msg" + (trace?.let { "\n$it" } ?: "")
    }
}

class JulFileHandler(private val file: RotatingLogFile) : Handler() {
    override fun publish(record: LogRecord) {
        val name = record.loggerName ?: return
        if (!name.startsWith("WS:") && !name.startsWith("dev.waystone")) return
        val level = when {
            record.level.intValue() >= Level.SEVERE.intValue() -> 'E'
            record.level.intValue() >= Level.WARNING.intValue() -> 'W'
            record.level.intValue() >= Level.INFO.intValue() -> 'I'
            else -> 'D'
        }
        runCatching {
            file.append(WsLogFile.format(level, name, record.message.orEmpty(), record.thrown))
        }
    }

    override fun flush() = Unit
    override fun close() = Unit
}
