package dev.waystone.app

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.lifecycleScope
import dev.waystone.app.nav.StartDestinationResolver
import dev.waystone.app.nav.WaystoneNavHost
import dev.waystone.app.ui.theme.WaystoneTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import uniffi.waystone_mobile.crc32Update

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ServiceLocator.init(applicationContext)
        lifecycleScope.launch(Dispatchers.IO) {
            runCatching { crc32Update(0u, ByteArray(0)) }
                .onSuccess { Log.i(TAG, "native engine loaded (crc32 probe=$it)") }
                .onFailure { Log.e(TAG, "native engine failed to load", it) }
        }
        lifecycleScope.launch {
            val serverUrl = runCatching { ServiceLocator.settingsStore.load().serverUrl }
                .onFailure { Log.w(TAG, "settings load failed; starting at setup", it) }
                .getOrDefault("")
            val startDestination = StartDestinationResolver.resolve(serverUrl)
            Log.i(TAG, "start destination: $startDestination")
            setContent {
                WaystoneTheme {
                    WaystoneNavHost(startDestination = startDestination)
                }
            }
        }
    }

    private companion object {
        const val TAG = "WS:APP"
    }
}
