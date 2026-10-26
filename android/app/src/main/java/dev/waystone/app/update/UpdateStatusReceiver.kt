package dev.waystone.app.update

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import dev.waystone.app.log.Log

class UpdateStatusReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
        Log.i(TAG, "install status=$status message=$message")
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                val confirm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                } else {
                    @Suppress("DEPRECATION") intent.getParcelableExtra(Intent.EXTRA_INTENT)
                }
                if (confirm == null) {
                    Log.e(TAG, "pending user action without confirm intent")
                    InstallEvents.results.value = InstallResult(false, "No confirmation screen")
                } else {
                    context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                    Log.i(TAG, "install confirmation shown")
                }
            }
            PackageInstaller.STATUS_SUCCESS -> InstallEvents.results.value = InstallResult(true, null)
            else -> InstallEvents.results.value = InstallResult(false, message ?: "status $status")
        }
    }

    private companion object { const val TAG = "WS:UPDATE" }
}
