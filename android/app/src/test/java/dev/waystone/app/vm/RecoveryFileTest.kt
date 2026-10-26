package dev.waystone.app.vm

import org.junit.Assert.assertEquals
import org.junit.Test

class RecoveryFileTest {
    @Test
    fun `name and content`() {
        assertEquals("recovery-dev-1.txt", RecoveryFile.defaultName("dev-1"))
        assertEquals("abcd\n", RecoveryFile.content("abcd"))
    }
}
