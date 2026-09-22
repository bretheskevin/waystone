package dev.waystone.app.vm

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SetupValidatorTest {

    @Test
    fun acceptsHttpAndHttpsUrls() {
        assertTrue(SetupValidator.isValidServerUrl("http://192.168.1.10:5000"))
        assertTrue(SetupValidator.isValidServerUrl("https://dav.example.com/waystone"))
        assertTrue(SetupValidator.isValidServerUrl("  https://dav.example.com "))
    }

    @Test
    fun rejectsInvalidUrls() {
        assertFalse(SetupValidator.isValidServerUrl(""))
        assertFalse(SetupValidator.isValidServerUrl("   "))
        assertFalse(SetupValidator.isValidServerUrl("dav.example.com"))
        assertFalse(SetupValidator.isValidServerUrl("ftp://dav.example.com"))
        assertFalse(SetupValidator.isValidServerUrl("http://"))
    }

    @Test
    fun validateFormReportsFirstProblem() {
        assertEquals(
            SetupValidator.URL_ERROR,
            SetupValidator.validateForm("dav.example.com", "pw", "pw"),
        )
        assertEquals(
            SetupValidator.PASSPHRASE_ERROR,
            SetupValidator.validateForm("https://dav.example.com", "", ""),
        )
        assertEquals(
            SetupValidator.CONFIRM_ERROR,
            SetupValidator.validateForm("https://dav.example.com", "pw", "px"),
        )
        assertNull(SetupValidator.validateForm("https://dav.example.com", "pw", "pw"))
    }
}
