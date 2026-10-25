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
    /** External save tree (SAF) for emulator presets whose saves live outside the ROM folder. */
    val saveUri: String? = null,
    /** EmulatorPreset.id, or null for a manual source. */
    val preset: String? = null,
) {
    /** Identity of a configured source: one ROM folder can back several presets. Not serialized. */
    val key: String get() = listOf(uri, adapter, system, saveUri.orEmpty(), preset.orEmpty()).joinToString("|")
}

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
