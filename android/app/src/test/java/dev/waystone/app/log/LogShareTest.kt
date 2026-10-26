package dev.waystone.app.log

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class LogShareTest {
    @get:Rule val tmp = TemporaryFolder()

    @Test
    fun `shares live log then old log, skipping missing and empty files`() {
        File(tmp.root, "waystone.log").writeText("a")
        File(tmp.root, "waystone.log.old").writeText("b")
        assertEquals(listOf("waystone.log", "waystone.log.old"), LogShare.filesToShare(tmp.root).map { it.name })
        File(tmp.root, "waystone.log.old").writeText("")
        assertEquals(listOf("waystone.log"), LogShare.filesToShare(tmp.root).map { it.name })
        File(tmp.root, "waystone.log").delete()
        assertEquals(emptyList<File>(), LogShare.filesToShare(tmp.root))
    }
}
