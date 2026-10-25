package dev.waystone.app.update

import kotlinx.coroutines.flow.MutableStateFlow

data class InstallResult(val success: Boolean, val message: String?)

/** Process-wide channel from UpdateStatusReceiver to the Settings VM. */
object InstallEvents {
    val results = MutableStateFlow<InstallResult?>(null)
}
