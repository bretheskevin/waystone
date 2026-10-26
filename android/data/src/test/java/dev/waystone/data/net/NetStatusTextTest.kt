package dev.waystone.data.net

import org.junit.Assert.assertEquals
import org.junit.Test

class NetStatusTextTest {
    @Test
    fun `strings match shell-common net_status`() {
        assertEquals("No internet connection detected.", NetStatusText.headline(NoInternetReason.NoNetwork))
        assertEquals("Can't reach your Waystone server.", NetStatusText.headline(NoInternetReason.ServerUnreachable))
        assertEquals("Check your Wi-Fi settings and try again.", NetStatusText.detail(NoInternetReason.NoNetwork))
        assertEquals("Check the server URL in Settings and try again.", NetStatusText.detail(NoInternetReason.ServerUnreachable))
        assertEquals("Still no connection. Try again.", NetStatusText.RETRY_SUFFIX)
    }
}
