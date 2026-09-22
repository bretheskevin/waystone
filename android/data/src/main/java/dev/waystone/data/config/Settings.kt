package dev.waystone.data.config

import java.util.UUID
import java.util.logging.Level
import java.util.logging.Logger
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

@Serializable
data class SourceFolder(
    val uri: String,
    val adapter: String,
    val system: String,
)

@Serializable
data class Settings(
    val deviceId: String,
    val serverUrl: String = "",
    val conflictPolicy: String = "newest-wins",
    val username: String? = null,
    val safetyBackup: Boolean = true,
    val sources: List<SourceFolder> = emptyList(),
)

object SettingsCodec {
    private val log = Logger.getLogger(SettingsCodec::class.java.name)

    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
    }

    fun encode(settings: Settings): String = json.encodeToString(settings)

    fun decode(raw: String?): Settings {
        if (raw == null) return fresh()
        return try {
            json.decodeFromString<Settings>(raw)
        } catch (e: Exception) {
            log.log(Level.WARNING, "settings decode failed, generating fresh deviceId", e)
            fresh()
        }
    }

    private fun fresh() = Settings(deviceId = UUID.randomUUID().toString())
}
