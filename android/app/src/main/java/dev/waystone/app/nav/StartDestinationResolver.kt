package dev.waystone.app.nav

object Routes {
    const val SETUP = "setup"
    const val UNLOCK = "unlock"
    const val DASHBOARD = "dashboard"
    const val CONFLICTS = "conflicts"
    const val HISTORY = "history/{groupKey}"
    const val SNAPSHOTS = "snapshots/{groupKey}"
    const val SETTINGS = "settings"
    const val NO_INTERNET = "no_internet"

    fun history(groupKey: String) = "history/${groupKey}"
    fun snapshots(groupKey: String) = "snapshots/${groupKey}"
}

object StartDestinationResolver {
    fun resolve(serverUrl: String?, online: Boolean): String = when {
        !online -> Routes.NO_INTERNET
        serverUrl.isNullOrBlank() -> Routes.SETUP
        else -> Routes.UNLOCK
    }
}
