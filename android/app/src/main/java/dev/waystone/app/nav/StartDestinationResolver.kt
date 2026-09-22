package dev.waystone.app.nav

object Routes {
    const val SETUP = "setup"
    const val UNLOCK = "unlock"
    const val DASHBOARD = "dashboard"
    const val CONFLICTS = "conflicts"
    const val HISTORY = "history/{groupKey}"
    const val SNAPSHOTS = "snapshots/{groupKey}"
    const val SETTINGS = "settings"

    fun history(groupKey: String) = "history/${groupKey}"
    fun snapshots(groupKey: String) = "snapshots/${groupKey}"
}

object StartDestinationResolver {
    fun resolve(serverUrl: String?): String =
        if (serverUrl.isNullOrBlank()) Routes.SETUP else Routes.UNLOCK
}
