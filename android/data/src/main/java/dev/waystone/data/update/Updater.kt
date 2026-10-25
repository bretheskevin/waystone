package dev.waystone.data.update

import java.io.File
import java.util.concurrent.TimeUnit
import java.util.logging.Level
import java.util.logging.Logger
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request

data class ReleaseInfo(val version: String, val assetUrl: String)

sealed interface ParseResult {
    data class Ok(val release: ReleaseInfo) : ParseResult
    data object NoAsset : ParseResult
    data object Malformed : ParseResult
}

sealed interface CheckResult {
    data class UpToDate(val latest: String) : CheckResult
    data class Available(val release: ReleaseInfo) : CheckResult
    data class Failed(val message: String) : CheckResult
}

/** Android port of shell-common/updater. Blocking: call on an IO dispatcher. */
class Updater(
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS).readTimeout(30, TimeUnit.SECONDS).build(),
    private val latestUrl: String = LATEST_URL,
) {
    private val log = Logger.getLogger("WS:UPDATE")

    fun checkLatest(currentVersion: String): CheckResult {
        log.info("check start: current=$currentVersion url=$latestUrl")
        val request = Request.Builder().url(latestUrl)
            .header("User-Agent", USER_AGENT).header("Accept", "application/vnd.github+json").build()
        val body = try {
            client.newCall(request).execute().use { resp ->
                if (!resp.isSuccessful) {
                    log.warning("check failed: HTTP ${resp.code}")
                    return CheckResult.Failed("GitHub returned HTTP ${resp.code}")
                }
                resp.body?.string().orEmpty()
            }
        } catch (e: Exception) {
            log.log(Level.WARNING, "check failed: network", e)
            return CheckResult.Failed("Network error: ${e.message ?: e.javaClass.simpleName}")
        }
        return when (val parsed = parseRelease(body)) {
            ParseResult.Malformed -> CheckResult.Failed("Could not read release info").also { log.warning("check failed: malformed JSON") }
            ParseResult.NoAsset -> CheckResult.Failed("Latest release has no $APK_SUFFIX build").also { log.warning("check: no $APK_SUFFIX asset") }
            is ParseResult.Ok -> {
                val r = parsed.release
                if (versionNewer(r.version, currentVersion)) {
                    log.info("check: newer version available v${r.version} (${r.assetUrl})")
                    CheckResult.Available(r)
                } else {
                    log.info("check: up to date (latest v${r.version}, current v$currentVersion)")
                    CheckResult.UpToDate(r.version)
                }
            }
        }
    }

    /** Streams [url] into [dest]; any failure or cancel deletes [dest] and returns false. */
    fun download(url: String, dest: File, onProgress: (done: Long, total: Long) -> Unit, isCancelled: () -> Boolean): Boolean {
        log.info("download start: $url -> ${dest.path}")
        var ok = false
        try {
            client.newCall(Request.Builder().url(url).header("User-Agent", USER_AGENT).build()).execute().use { resp ->
                val body = resp.body
                if (!resp.isSuccessful || body == null) {
                    log.warning("download failed: HTTP ${resp.code}")
                } else {
                    val total = body.contentLength()
                    dest.parentFile?.mkdirs()
                    var done = 0L
                    var cancelled = false
                    body.byteStream().use { input ->
                        dest.outputStream().use { out ->
                            val buf = ByteArray(64 * 1024)
                            while (true) {
                                if (isCancelled()) {
                                    cancelled = true
                                    break
                                }
                                val n = input.read(buf)
                                if (n < 0) break
                                out.write(buf, 0, n)
                                done += n
                                onProgress(done, total)
                            }
                        }
                    }
                    if (cancelled) {
                        log.info("download cancelled at $done bytes")
                    } else {
                        log.info("download done: $done bytes")
                        ok = true
                    }
                }
            }
        } catch (e: Exception) {
            log.log(Level.WARNING, "download failed", e)
            ok = false
        }
        if (!ok && dest.exists() && !dest.delete()) log.warning("could not delete partial ${dest.path}")
        return ok
    }

    companion object {
        const val LATEST_URL = "https://api.github.com/repos/bretheskevin/waystone/releases/latest"
        const val APK_SUFFIX = ".apk"
        private const val USER_AGENT = "waystone"
        private val json = Json { ignoreUnknownKeys = true }

        fun parseRelease(body: String, suffix: String = APK_SUFFIX): ParseResult {
            val obj: JsonObject = try {
                json.parseToJsonElement(body).jsonObject
            } catch (e: Exception) {
                return ParseResult.Malformed
            }
            val tag = runCatching { obj["tag_name"]?.jsonPrimitive?.contentOrNull }.getOrNull()
                ?: return ParseResult.Malformed
            val url = runCatching { obj["assets"]?.jsonArray.orEmpty() }.getOrDefault(emptyList())
                .firstNotNullOfOrNull { a ->
                    runCatching { a.jsonObject["browser_download_url"]?.jsonPrimitive?.contentOrNull }.getOrNull()
                        ?.takeIf { it.endsWith(suffix) }
                } ?: return ParseResult.NoAsset
            return ParseResult.Ok(ReleaseInfo(tag.removePrefix("v"), url))
        }

        /** Per-character port of shell-common version_newer: numeric components, malformed = 0. */
        fun versionNewer(latest: String, current: String): Boolean {
            var l = 0
            var c = 0
            while (true) {
                val (lv, ln) = component(latest, l)
                val (cv, cn) = component(current, c)
                l = ln
                c = cn
                if (lv > cv) return true
                if (lv < cv) return false
                if (l >= latest.length && c >= current.length) return false
                if (l < latest.length) l++
                if (c < current.length) c++
            }
        }

        private fun component(s: String, start: Int): Pair<Long, Int> {
            var i = start
            var v = 0L
            while (i < s.length && s[i] in '0'..'9') {
                v = v * 10 + (s[i] - '0')
                i++
            }
            return v to i
        }
    }
}
