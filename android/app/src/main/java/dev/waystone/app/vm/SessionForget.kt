package dev.waystone.app.vm

/**
 * Drops this device's login (wrapped MDK + WebDAV creds) and the in-memory session.
 * Settings and server data are left untouched.
 */
internal suspend fun forgetSession(
    store: SessionVaultStore,
    onSessionCleared: () -> Unit,
    log: (String) -> Unit,
) {
    log("forget session: clearing stored MDK + WebDAV creds")
    store.clear()
    onSessionCleared()
    log("forget session: stored login and in-memory session cleared")
}
