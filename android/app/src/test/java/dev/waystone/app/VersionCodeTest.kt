package dev.waystone.app

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Test

class VersionCodeTest {
    // Gradle runs unit tests with the module dir (android/app) as working directory.
    private val version = File("../../VERSION").readText().trim()

    @Test
    fun `versionName comes from the root VERSION file`() {
        assertEquals(version, BuildConfig.VERSION_NAME)
    }

    @Test
    fun `versionCode is major*10000 + minor*100 + patch`() {
        val (major, minor, patch) = version.split('.').map(String::toInt)
        assertEquals(major * 10000 + minor * 100 + patch, BuildConfig.VERSION_CODE)
    }
}
