package dev.waystone.data.config

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsCodecTest {

    @Test
    fun `encode decode round-trip preserves all fields`() {
        val settings = Settings(
            deviceId = "dev-1",
            serverUrl = "https://dav.example.com",
            conflictPolicy = "remote-wins",
            username = "alice",
            safetyBackup = false,
            sources = listOf(SourceFolder("content://a", "jksv", "switch")),
        )
        assertEquals(settings, SettingsCodec.decode(SettingsCodec.encode(settings)))
    }

    @Test
    fun `legacy json missing new fields gets defaults`() {
        val raw = """{"deviceId":"d1","serverUrl":"https://x","conflictPolicy":"newest-wins","username":"bob"}"""
        val s = SettingsCodec.decode(raw)
        assertEquals("d1", s.deviceId)
        assertEquals("https://x", s.serverUrl)
        assertEquals("bob", s.username)
        assertTrue(s.safetyBackup)          // default true
        assertEquals(emptyList<SourceFolder>(), s.sources)
    }

    @Test
    fun `garbage returns fresh settings with generated deviceId`() {
        val s = SettingsCodec.decode("not json{")
        assertTrue(s.deviceId.isNotBlank())
        assertEquals("", s.serverUrl)
        assertTrue(s.safetyBackup)
        assertEquals(emptyList<SourceFolder>(), s.sources)
    }

    @Test
    fun `null returns fresh settings with distinct generated deviceIds`() {
        val a = SettingsCodec.decode(null)
        val b = SettingsCodec.decode(null)
        assertTrue(a.deviceId.isNotBlank())
        assertNotEquals(a.deviceId, b.deviceId)
    }

    @Test
    fun `encode writes defaults explicitly`() {
        val raw = SettingsCodec.encode(Settings(deviceId = "d1"))
        assertTrue(raw.contains("\"safetyBackup\":true"))
        assertTrue(raw.contains("\"sources\":[]"))
        assertTrue(raw.contains("\"serverUrl\":\"\""))
    }

    @Test
    fun `decode ignores unknown keys`() {
        val raw = """{"deviceId":"d1","futureField":42,"other":"x"}"""
        assertEquals("d1", SettingsCodec.decode(raw).deviceId)
    }

    @Test
    fun `serverChanged matrix`() {
        val base = Settings(deviceId = "d", serverUrl = "https://a")
        val same = base.copy(username = "bob")
        val changed = base.copy(serverUrl = "https://b")

        assertTrue(SettingsStore.serverChanged(null, base))
        assertTrue(SettingsStore.serverChanged(base, changed))
        assertTrue(SettingsStore.serverChanged(base.copy(serverUrl = ""), base))
        assertTrue(!SettingsStore.serverChanged(base, same))
    }

    @Test
    fun `serverChanged null old treated as changed even for empty new url`() {
        assertTrue(SettingsStore.serverChanged(null, Settings(deviceId = "d")))
    }

    @Test
    fun `default username is null`() {
        assertNull(Settings(deviceId = "d").username)
    }

    @Test
    fun `old source json without saveUri and preset still decodes`() {
        val raw = """{"deviceId":"d1","sources":[{"uri":"content://roms","adapter":"rom_keyed","system":""}]}"""
        val s = SettingsCodec.decode(raw)
        assertEquals(SourceFolder("content://roms", "rom_keyed", "", saveUri = null, preset = null), s.sources.single())
    }

    @Test
    fun `preset source round-trips and maps to its flavor`() {
        val src = SourceFolder("content://roms", "rom_keyed", "", saveUri = "content://saves", preset = "retroarch")
        val s = Settings(deviceId = "d", sources = listOf(src))
        assertEquals(s, SettingsCodec.decode(SettingsCodec.encode(s)))
        assertEquals("retroarch", src.externalFlavor())
        assertEquals(null, src.copy(saveUri = null).externalFlavor())
        assertEquals("native", src.copy(preset = null).externalFlavor())
    }

    @Test
    fun `one rom folder under two presets yields distinct unserialized keys`() {
        val retro = SourceFolder("content://roms", "rom_keyed", "", saveUri = "content://ra", preset = "retroarch")
        val drastic = retro.copy(saveUri = "content://ds", preset = "drastic")
        assertNotEquals(retro.key, drastic.key)
        assertFalse(SettingsCodec.encode(Settings(deviceId = "d", sources = listOf(retro))).contains("\"key\""))
    }
}
