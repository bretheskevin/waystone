package dev.waystone.data.update

import java.io.File
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okio.Buffer
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import uniffi.waystone_mobile.versionNewer

class UpdaterTest {
    @get:Rule val tmp = TemporaryFolder()
    private val server = MockWebServer()

    @After fun tearDown() = server.shutdown()

    @Test
    fun `versionNewer matches shell-common version_newer cases`() {
        assertTrue(versionNewer("1.10.0", "1.9.0"))
        assertFalse(versionNewer("1.9.0", "1.10.0"))
        assertTrue(versionNewer("0.2.0", "0.1.0"))
        assertTrue(versionNewer("2", "1.9.9"))
        assertTrue(versionNewer("1.0.1", "1.0"))
        assertFalse(versionNewer("1.0.0", "1.0.0"))
        assertFalse(versionNewer("1.0", "1.0.0"))
        assertFalse(versionNewer("1.0.0", "1.0"))
        assertFalse(versionNewer("1.0.0", "1.0.0.0"))
        assertFalse(versionNewer("abc", "1.0.0"))
        assertFalse(versionNewer("", ""))
        assertFalse(versionNewer("1.0a", "1.0"))
        assertTrue(versionNewer("0.6.0", "0.5.4"))
    }

    private val release = """{"tag_name":"v0.6.0","assets":[
        {"name":"waystone-3ds-spike.3dsx","browser_download_url":"https://x/waystone-3ds-spike.3dsx"},
        {"name":"waystone-3ds-spike.cia","browser_download_url":"https://x/waystone-3ds-spike.cia"},
        {"name":"waystone.nro","browser_download_url":"https://x/waystone.nro"},
        {"name":"waystone.apk","browser_download_url":"https://x/waystone.apk"}],
        "body":"notes with \"escapes\""}"""

    @Test
    fun `parseRelease picks the apk among console distractors and strips v`() {
        assertEquals(ParseResult.Ok(ReleaseInfo("0.6.0", "https://x/waystone.apk")), Updater.parseRelease(release))
    }

    @Test
    fun `parseRelease reports missing asset, missing tag and bad json`() {
        assertEquals(ParseResult.NoAsset, Updater.parseRelease("""{"tag_name":"v1.0.0","assets":[{"browser_download_url":"https://x/a.nro"}]}"""))
        assertEquals(ParseResult.NoAsset, Updater.parseRelease("""{"tag_name":"v1.0.0"}"""))
        assertEquals(ParseResult.Malformed, Updater.parseRelease("""{"assets":[]}"""))
        assertEquals(ParseResult.Malformed, Updater.parseRelease("not json"))
    }

    @Test
    fun `checkLatest sends UA waystone and reports Available or UpToDate`() {
        server.enqueue(MockResponse().setBody(release))
        server.enqueue(MockResponse().setBody(release))
        val updater = Updater(latestUrl = server.url("/releases/latest").toString())

        assertEquals(CheckResult.Available(ReleaseInfo("0.6.0", "https://x/waystone.apk")), updater.checkLatest("0.5.4"))
        assertEquals("waystone", server.takeRequest().getHeader("User-Agent"))
        assertEquals(CheckResult.UpToDate("0.6.0"), updater.checkLatest("0.6.0"))
    }

    @Test
    fun `checkLatest maps HTTP and parse failures to Failed`() {
        server.enqueue(MockResponse().setResponseCode(403))
        server.enqueue(MockResponse().setBody("""{"tag_name":"v9.9.9","assets":[]}"""))
        val updater = Updater(latestUrl = server.url("/releases/latest").toString())
        assertEquals(CheckResult.Failed("GitHub returned HTTP 403"), updater.checkLatest("0.5.4"))
        assertEquals(CheckResult.Failed("Latest release has no .apk build"), updater.checkLatest("0.5.4"))
    }

    @Test
    fun `download writes the file and reports progress`() {
        val body = ByteArray(200_000) { it.toByte() }
        server.enqueue(MockResponse().setBody(Buffer().write(body)))
        val dest = File(tmp.root, "update.apk")
        var last = 0L
        val ok = Updater().download(server.url("/a.apk").toString(), dest, { done, _ -> last = done }, { false })
        assertTrue(ok)
        assertEquals(body.size.toLong(), dest.length())
        assertEquals(body.size.toLong(), last)
    }

    @Test
    fun `download failure or cancel deletes the partial file`() {
        server.enqueue(MockResponse().setResponseCode(500))
        server.enqueue(MockResponse().setBody(Buffer().write(ByteArray(500_000))))
        val dest = File(tmp.root, "update.apk")
        assertFalse(Updater().download(server.url("/a.apk").toString(), dest, { _, _ -> }, { false }))
        assertFalse(dest.exists())
        assertFalse(Updater().download(server.url("/a.apk").toString(), dest, { _, _ -> }, { true }))
        assertFalse(dest.exists())
    }
}
