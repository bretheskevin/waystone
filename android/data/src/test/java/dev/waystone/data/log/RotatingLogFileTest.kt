package dev.waystone.data.log

import java.io.File
import java.util.logging.Level
import java.util.logging.LogRecord
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class RotatingLogFileTest {
    @get:Rule val tmp = TemporaryFolder()

    @Test
    fun `file past max rotates to old and discards the previous old`() {
        val f = File(tmp.root, "waystone.log")
        File(tmp.root, "waystone.log.old").writeText("ancient")
        f.writeText("x".repeat(20))
        RotatingLogFile(f, maxBytes = 10)
        assertFalse(f.exists())
        assertEquals("x".repeat(20), File(tmp.root, "waystone.log.old").readText())
    }

    @Test
    fun `file under max is untouched and append rotates when it grows past max`() {
        val f = File(tmp.root, "logs/waystone.log")
        val log = RotatingLogFile(f, maxBytes = 10)
        log.append("abc")
        assertEquals("abc\n", f.readText())
        log.append("0123456789")
        log.append("next")
        assertEquals("next\n", f.readText())
        assertEquals("abc\n0123456789\n", File(f.path + ".old").readText())
    }

    @Test
    fun `JulFileHandler writes WS records and ignores other loggers`() {
        val f = File(tmp.root, "waystone.log")
        val h = JulFileHandler(RotatingLogFile(f))
        h.publish(LogRecord(Level.INFO, "pushed").apply { loggerName = "WS:SYNC" })
        h.publish(LogRecord(Level.INFO, "noise").apply { loggerName = "okhttp3.OkHttpClient" })
        val text = f.readText()
        assertTrue(text, text.contains(" I WS:SYNC pushed"))
        assertFalse(text.contains("noise"))
    }

    @Test
    fun `format puts level, tag, message and stack trace on the line`() {
        val line = WsLogFile.format('E', "WS:X", "boom", IllegalStateException("bad"))
        assertTrue(line, Regex("""^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z E WS:X boom\n.*IllegalStateException: bad""", RegexOption.DOT_MATCHES_ALL).containsMatchIn(line))
    }
}
