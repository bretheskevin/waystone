package dev.waystone.data.dav

import java.io.IOException
import java.util.concurrent.TimeUnit
import java.util.logging.Level
import java.util.logging.Logger
import okhttp3.Credentials
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import uniffi.waystone_mobile.WaystoneException
import uniffi.waystone_mobile.WebDav

private val XML_MEDIA_TYPE = "application/xml".toMediaType()

class OkHttpWebDav(
    baseUrl: String,
    private val username: String? = null,
    private val password: String? = null,
) : WebDav {

    private val log = Logger.getLogger(OkHttpWebDav::class.java.name)
    private val base = baseUrl.trimEnd('/')
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .writeTimeout(120, TimeUnit.SECONDS)
        .build()

    private fun url(path: String) = "$base/${path.trimStart('/')}"

    private fun Request.Builder.withAuth(): Request.Builder {
        if (!username.isNullOrBlank() && !password.isNullOrBlank()) {
            header("Authorization", Credentials.basic(username, password))
        }
        return this
    }

    private fun execute(method: String, path: String, body: String? = null, depth: String? = null): Response {
        val builder = Request.Builder().url(url(path))
        builder.method(method, body?.toRequestBody(XML_MEDIA_TYPE))
        if (depth != null) builder.header("Depth", depth)
        builder.withAuth()
        return try {
            client.newCall(builder.build()).execute()
        } catch (e: IOException) {
            throw WaystoneException.WebDav("$method $path failed: ${e.message}")
        }
    }

    override fun `get`(path: String): ByteArray? {
        execute("GET", path).use { resp ->
            when {
                resp.code == 404 -> {
                    log.info("GET $path -> 404 (absent)")
                    return null
                }
                !resp.isSuccessful -> throw WaystoneException.WebDav("GET $path returned ${resp.code}")
                else -> {
                    val data = resp.body?.bytes() ?: ByteArray(0)
                    log.info("GET $path -> ${data.size} bytes")
                    return data
                }
            }
        }
    }

    override fun `put`(path: String, body: ByteArray) {
        val reqBody = body.toRequestBody(XML_MEDIA_TYPE)
        val builder = Request.Builder().url(url(path)).put(reqBody).withAuth()
        val resp = try {
            client.newCall(builder.build()).execute()
        } catch (e: IOException) {
            throw WaystoneException.WebDav("PUT $path failed: ${e.message}")
        }
        resp.use {
            if (!it.isSuccessful) throw WaystoneException.WebDav("PUT $path returned ${it.code}")
            log.info("PUT $path -> ${it.code}")
        }
    }

    override fun `exists`(path: String): Boolean {
        execute("HEAD", path).use { resp ->
            log.info("HEAD $path -> ${resp.code}")
            return resp.isSuccessful
        }
    }

    override fun `propfind`(path: String): List<String> {
        execute("PROPFIND", path, PROPFIND_BODY, depth = "1").use { resp ->
            when {
                resp.code == 404 -> {
                    log.info("PROPFIND $path -> 404 (empty listing)")
                    return emptyList()
                }
                !resp.isSuccessful -> throw WaystoneException.WebDav("PROPFIND $path returned ${resp.code}")
                else -> {
                    val hrefs = parsePropfindHrefs(resp.body?.string() ?: "")
                    log.info("PROPFIND $path -> ${hrefs.size} hrefs")
                    return hrefs
                }
            }
        }
    }

    override fun `mkdirP`(path: String) {
        val segments = path.split('/').filter { it.isNotEmpty() }
        var current = ""
        for (seg in segments) {
            current = "$current/$seg"
            execute("MKCOL", current).use { resp ->
                if (resp.code != 201 && resp.code != 405 && !resp.isSuccessful) {
                    throw WaystoneException.WebDav("MKCOL $current returned ${resp.code}")
                }
                log.info("MKCOL $current -> ${resp.code}")
            }
        }
    }

    companion object {
        private const val PROPFIND_BODY =
            """<?xml version="1.0"?><D:propfind xmlns:D="DAV:"><D:prop><D:resourcetype/></D:prop></D:propfind>"""

        internal fun parsePropfindHrefs(xml: String): List<String> {
            val hrefs = mutableListOf<String>()
            val parts = xml.split("href>")
            var i = 1
            while (i < parts.size) {
                val part = parts[i]
                val end = part.indexOf('<')
                if (end >= 0) {
                    val href = part.substring(0, end).trim()
                    if (href.isNotEmpty()) hrefs.add(href)
                }
                i += 2
            }
            return hrefs
        }
    }
}
