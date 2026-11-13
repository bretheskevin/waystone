package dev.waystone.app.vm

import dev.waystone.data.config.Settings
import org.junit.Assert.assertEquals
import org.junit.Test

class SetupPrefillTest {

    @Test
    fun `blank settings give empty prefill`() {
        assertEquals(SetupPrefill("", ""), SetupPrefill.of(Settings(deviceId = "d")))
    }

    @Test
    fun `configured settings give server url and username`() {
        val settings = Settings(deviceId = "d", serverUrl = "https://dav.example", username = "alice")

        assertEquals(SetupPrefill("https://dav.example", "alice"), SetupPrefill.of(settings))
    }
}
