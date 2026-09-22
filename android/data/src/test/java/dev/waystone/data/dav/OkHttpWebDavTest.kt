package dev.waystone.data.dav

import okhttp3.Credentials
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import uniffi.waystone_mobile.WaystoneException

class OkHttpWebDavTest {

    private lateinit var server: MockWebServer
    private lateinit var dav: OkHttpWebDav

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        dav = OkHttpWebDav(server.url("/").toString())
    }

    @After
    fun tearDown() {
        server.shutdown()
    }

    @Test
    fun `get returns body on 2xx and null on 404`() {
        server.enqueue(MockResponse().setResponseCode(200).setBody("hello"))
        server.enqueue(MockResponse().setResponseCode(404))

        assertArrayEquals("hello".toByteArray(), dav.get("/a/b.txt"))
        assertNull(dav.get("/missing.bin"))

        val req = server.takeRequest()
        assertEquals("GET", req.method)
        assertEquals("/a/b.txt", req.path)
    }

    @Test
    fun `get throws WebDav exception with code on other errors`() {
        server.enqueue(MockResponse().setResponseCode(500))
        try {
            dav.get("/boom")
            fail("expected WaystoneException.WebDav")
        } catch (e: WaystoneException.WebDav) {
            assertEquals("GET /boom returned 500", e.msg)
        }
    }

    @Test
    fun `put sends body with PUT method and throws on non-2xx`() {
        server.enqueue(MockResponse().setResponseCode(201))

        dav.put("/blob/x.bin", byteArrayOf(1, 2, 3))

        val req = server.takeRequest()
        assertEquals("PUT", req.method)
        assertEquals("/blob/x.bin", req.path)
        assertArrayEquals(byteArrayOf(1, 2, 3), req.body.readByteArray())

        server.enqueue(MockResponse().setResponseCode(403))
        try {
            dav.put("/blob/y.bin", byteArrayOf(9))
            fail("expected WaystoneException.WebDav")
        } catch (e: WaystoneException.WebDav) {
            assertEquals("PUT /blob/y.bin returned 403", e.msg)
        }
    }

    @Test
    fun `exists issues HEAD and maps 404 to false`() {
        server.enqueue(MockResponse().setResponseCode(200))
        server.enqueue(MockResponse().setResponseCode(404))

        assertTrue(dav.exists("/present"))
        assertFalse(dav.exists("/absent"))

        assertEquals("HEAD", server.takeRequest().method)
        assertEquals("HEAD", server.takeRequest().method)
    }

    @Test
    fun `propfind sends Depth header and body, parses hrefs, 404 gives empty list`() {
        val xml = """<?xml version="1.0"?>
<D:multistatus xmlns:D="DAV:">
  <D:response>
    <D:href>/data/heads/</D:href>
    <D:propstat><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
  </D:response>
  <D:response>
    <D:href>/data/heads/device1.json</D:href>
    <D:propstat><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
  </D:response>
</D:multistatus>"""
        server.enqueue(MockResponse().setResponseCode(207).setBody(xml).addHeader("Content-Type", "application/xml"))

        val hrefs = dav.propfind("/data/heads")

        assertEquals(listOf("/data/heads/", "/data/heads/device1.json"), hrefs)

        val req = server.takeRequest()
        assertEquals("PROPFIND", req.method)
        assertEquals("1", req.getHeader("Depth"))
        assertEquals(
            """<?xml version="1.0"?><D:propfind xmlns:D="DAV:"><D:prop><D:resourcetype/></D:prop></D:propfind>""",
            req.body.readUtf8(),
        )

        server.enqueue(MockResponse().setResponseCode(404))
        assertEquals(emptyList<String>(), dav.propfind("/none"))
    }

    @Test
    fun `mkdirP issues cumulative MKCOL accepting 201, 405 and 2xx`() {
        server.enqueue(MockResponse().setResponseCode(201))
        server.enqueue(MockResponse().setResponseCode(405))
        server.enqueue(MockResponse().setResponseCode(200))

        dav.mkdirP("a/b/c")

        val reqs = (1..3).map { server.takeRequest() }
        assertEquals(listOf("MKCOL", "MKCOL", "MKCOL"), reqs.map { it.method })
        assertEquals(listOf("/a", "/a/b", "/a/b/c"), reqs.map { it.path })

        server.enqueue(MockResponse().setResponseCode(409))
        try {
            dav.mkdirP("x/y")
            fail("expected WaystoneException.WebDav")
        } catch (e: WaystoneException.WebDav) {
            assertEquals("MKCOL /x returned 409", e.msg)
        }
    }

    @Test
    fun `auth header present only when both creds present`() {
        val authed = OkHttpWebDav(server.url("/").toString(), username = "user", password = "pass")
        server.enqueue(MockResponse().setResponseCode(200).setBody("x"))
        authed.get("/secret")
        assertEquals(Credentials.basic("user", "pass"), server.takeRequest().getHeader("Authorization"))

        server.enqueue(MockResponse().setResponseCode(200).setBody("x"))
        dav.get("/open")
        assertNull(server.takeRequest().getHeader("Authorization"))

        val partial = OkHttpWebDav(server.url("/").toString(), username = "user", password = null)
        server.enqueue(MockResponse().setResponseCode(200).setBody("x"))
        partial.get("/open2")
        assertNull(server.takeRequest().getHeader("Authorization"))
    }
}
