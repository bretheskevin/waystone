package dev.waystone.data.config

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import java.util.logging.Logger
import kotlinx.coroutines.flow.first

private val Context.settingsDataStore by preferencesDataStore("waystone_settings")

class SettingsStore(private val context: Context) {

    private val log = Logger.getLogger(SettingsStore::class.java.name)
    private val key = stringPreferencesKey("settings_json")

    suspend fun load(): Settings {
        val raw = context.settingsDataStore.data.first()[key]
        val settings = SettingsCodec.decode(raw)
        log.info("settings loaded: deviceId=${settings.deviceId} server=${settings.serverUrl} sources=${settings.sources.size}")
        return settings
    }

    suspend fun save(settings: Settings) {
        context.settingsDataStore.edit { it[key] = SettingsCodec.encode(settings) }
        log.info("settings saved: deviceId=${settings.deviceId}")
    }

    companion object {
        fun serverChanged(old: Settings?, new: Settings): Boolean =
            old == null || old.serverUrl != new.serverUrl
    }
}
