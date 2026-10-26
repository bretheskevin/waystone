package dev.waystone.app.nav

import org.junit.Assert.assertEquals
import org.junit.Test

class StartDestinationResolverTest {

    @Test
    fun blankServerUrlStartsAtSetup() {
        assertEquals(Routes.SETUP, StartDestinationResolver.resolve("", true))
        assertEquals(Routes.SETUP, StartDestinationResolver.resolve("   ", true))
        assertEquals(Routes.SETUP, StartDestinationResolver.resolve(null, true))
    }

    @Test
    fun configuredServerStartsAtUnlock() {
        assertEquals(Routes.UNLOCK, StartDestinationResolver.resolve("https://dav.example.com", true))
        assertEquals(Routes.UNLOCK, StartDestinationResolver.resolve("http://192.168.1.10:5000", true))
    }

    @Test
    fun offlineStartsAtNoInternet() {
        assertEquals(Routes.NO_INTERNET, StartDestinationResolver.resolve("https://dav.example.com", false))
        assertEquals(Routes.NO_INTERNET, StartDestinationResolver.resolve("", false))
    }
}
