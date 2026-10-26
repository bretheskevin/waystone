package dev.waystone.app.net

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import dev.waystone.app.vm.NetworkMonitor

class Connectivity(private val context: Context) : NetworkMonitor {
    override fun isOnline(): Boolean {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }
}
