package dev.waystone.data.net

enum class NoInternetReason { NoNetwork, ServerUnreachable }

/** Strings copied from shell-common/net_status.h. */
object NetStatusText {
    fun headline(r: NoInternetReason) = when (r) {
        NoInternetReason.NoNetwork -> "No internet connection detected."
        NoInternetReason.ServerUnreachable -> "Can't reach your Waystone server."
    }

    fun detail(r: NoInternetReason) = when (r) {
        NoInternetReason.NoNetwork -> "Check your Wi-Fi settings and try again."
        NoInternetReason.ServerUnreachable -> "Check the server URL in Settings and try again."
    }

    const val RETRY_SUFFIX = "Still no connection. Try again."
}
