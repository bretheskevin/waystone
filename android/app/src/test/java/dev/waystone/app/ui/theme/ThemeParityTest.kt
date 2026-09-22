package dev.waystone.app.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import org.junit.Assert.assertEquals
import org.junit.Test

class ThemeParityTest {

    private fun argb(color: Color): Int = color.toArgb()

    @Test
    fun primaryAndStatusColorsMatchDesignTokens() {
        assertEquals(0xFF6366F1.toInt(), argb(Primary500))
        assertEquals(0xFF22C55E.toInt(), argb(Success))
        assertEquals(0xFFF59E0B.toInt(), argb(Warning))
        assertEquals(0xFFEF4444.toInt(), argb(Error))
        assertEquals(0xFF06B6D4.toInt(), argb(Sync))
        assertEquals(0xFF4F46E5.toInt(), argb(LightPrimary))
        assertEquals(0xFF818CF8.toInt(), argb(DarkPrimary))
    }

    @Test
    fun primaryScaleMatchesDesignTokens() {
        assertEquals(0xFFEEF2FF.toInt(), argb(Primary50))
        assertEquals(0xFFE0E7FF.toInt(), argb(Primary100))
        assertEquals(0xFFC7D2FE.toInt(), argb(Primary200))
        assertEquals(0xFFA5B4FC.toInt(), argb(Primary300))
        assertEquals(0xFF818CF8.toInt(), argb(Primary400))
        assertEquals(0xFF6366F1.toInt(), argb(Primary500))
        assertEquals(0xFF4F46E5.toInt(), argb(Primary600))
        assertEquals(0xFF4338CA.toInt(), argb(Primary700))
        assertEquals(0xFF3730A3.toInt(), argb(Primary800))
        assertEquals(0xFF312E81.toInt(), argb(Primary900))
    }

    @Test
    fun neutralScaleMatchesDesignTokens() {
        assertEquals(0xFFFAFAFA.toInt(), argb(Neutral50))
        assertEquals(0xFFF4F4F5.toInt(), argb(Neutral100))
        assertEquals(0xFFE4E4E7.toInt(), argb(Neutral200))
        assertEquals(0xFFD4D4D8.toInt(), argb(Neutral300))
        assertEquals(0xFFA1A1AA.toInt(), argb(Neutral400))
        assertEquals(0xFF71717A.toInt(), argb(Neutral500))
        assertEquals(0xFF52525B.toInt(), argb(Neutral600))
        assertEquals(0xFF3F3F46.toInt(), argb(Neutral700))
        assertEquals(0xFF27272A.toInt(), argb(Neutral800))
        assertEquals(0xFF18181B.toInt(), argb(Neutral900))
    }

    @Test
    fun statusColorMapsStatusesToPalette() {
        assertEquals(Success, statusColor("in_sync"))
        assertEquals(Success, statusColor("inSync"))
        assertEquals(Sync, statusColor("ahead"))
        assertEquals(Sync, statusColor("behind"))
        assertEquals(Warning, statusColor("conflict"))
        assertEquals(Error, statusColor("error"))
        assertEquals(Neutral400, statusColor("unknown"))
    }

    @Test
    fun statusColorAcceptsSourceStatusNames() {
        assertEquals(Success, statusColor("InSync"))
        assertEquals(Sync, statusColor("Ahead"))
        assertEquals(Sync, statusColor("Behind"))
        assertEquals(Warning, statusColor("Conflict"))
        assertEquals(Error, statusColor("Error"))
    }
}
