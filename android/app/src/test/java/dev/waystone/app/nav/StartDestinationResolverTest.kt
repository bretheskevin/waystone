package dev.waystone.app.nav

import org.junit.Assert.assertEquals
import org.junit.Test

class StartDestinationResolverTest {

    @Test
    fun blankServerUrlStartsAtSetup() {
        assertEquals(Routes.SETUP, StartDestinationResolver.resolve(""))
        assertEquals(Routes.SETUP, StartDestinationResolver.resolve("   "))
        assertEquals(Routes.SETUP, StartDestinationResolver.resolve(null))
    }

    @Test
    fun configuredServerStartsAtUnlock() {
        assertEquals(Routes.UNLOCK, StartDestinationResolver.resolve("https://dav.example.com"))
        assertEquals(Routes.UNLOCK, StartDestinationResolver.resolve("http://192.168.1.10:5000"))
    }
}
