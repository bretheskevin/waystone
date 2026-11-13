package dev.waystone.app.vm

import android.app.Application
import android.content.Intent
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.waystone.app.log.Log
import dev.waystone.app.update.ApkInstaller
import dev.waystone.app.update.InstallEvents
import dev.waystone.data.update.Updater
import java.io.File
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/** Activity-scoped owner of the self-update flow, shared by Settings, Unlock and Setup. */
class UpdateViewModel(
    application: Application,
    appVersion: String,
) : AndroidViewModel(application) {

    private val releases = Updater()

    private val updater = UpdateController(
        scope = viewModelScope,
        currentVersion = appVersion,
        check = { current -> releases.checkLatest(current) },
        download = { url, onProgress, isCancelled ->
            val dest = File(getApplication<Application>().cacheDir, "update.apk")
            dest.takeIf { releases.download(url, it, onProgress, isCancelled) }
        },
        canInstall = { getApplication<Application>().packageManager.canRequestPackageInstalls() },
        install = { apk -> ApkInstaller.install(getApplication(), apk) },
    )

    val phase: StateFlow<UpdatePhase> = updater.phase

    init {
        viewModelScope.launch {
            InstallEvents.results.collect { r ->
                if (r != null) {
                    updater.onInstallResult(r.success, r.message)
                    InstallEvents.results.value = null
                }
            }
        }
    }

    fun check() {
        Log.i(TAG, "check for updates requested")
        updater.check()
    }

    fun downloadAndInstall() {
        Log.i(TAG, "download & install requested")
        updater.downloadAndInstall()
    }

    fun cancel() {
        Log.i(TAG, "update download cancel requested")
        updater.cancel()
    }

    fun openInstallPermissionSettings() {
        val app = getApplication<Application>()
        Log.i(TAG, "opening unknown-sources settings for ${app.packageName}")
        app.startActivity(
            Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    }

    private companion object {
        const val TAG = "WS:UPDATE"
    }
}
