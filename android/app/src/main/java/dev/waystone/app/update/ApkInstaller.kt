package dev.waystone.app.update

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import dev.waystone.app.log.Log
import java.io.File

object ApkInstaller {
    const val ACTION_INSTALL_STATUS = "dev.waystone.app.INSTALL_STATUS"
    private const val TAG = "WS:UPDATE"

    /** Streams [apk] into a PackageInstaller session; the result arrives in UpdateStatusReceiver. */
    fun install(context: Context, apk: File) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(context.packageName)
        val sessionId = installer.createSession(params)
        Log.i(TAG, "install session $sessionId created (${apk.length()} bytes)")
        try {
            installer.openSession(sessionId).use { session ->
                apk.inputStream().use { input ->
                    session.openWrite("waystone.apk", 0, apk.length()).use { out ->
                        input.copyTo(out)
                        session.fsync(out)
                    }
                }
                val intent = Intent(context, UpdateStatusReceiver::class.java).setAction(ACTION_INSTALL_STATUS)
                // PackageInstaller fills extras into this intent, so it must be mutable (explicit target).
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                val pending = PendingIntent.getBroadcast(context, sessionId, intent, flags)
                session.commit(pending.intentSender)
                Log.i(TAG, "install session $sessionId committed")
            }
        } catch (e: Exception) {
            Log.e(TAG, "install session $sessionId failed", e)
            runCatching { installer.abandonSession(sessionId) }
            throw e
        }
    }
}
