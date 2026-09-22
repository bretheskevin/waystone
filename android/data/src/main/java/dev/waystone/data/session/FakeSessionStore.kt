package dev.waystone.data.session

/**
 * In-memory [SessionStore] for tests and previews. Mirrors the contract of
 * [KeystoreVaultStore]: caller arrays are zeroized, corrupt/absent state reads as null.
 */
class FakeSessionStore : SessionStore {

    /** When true, reads simulate a corrupt/undecryptable blob (null results). */
    var simulateCorruption: Boolean = false

    private var mdk: ByteArray? = null
    private var creds: Pair<String, CharArray>? = null

    override fun wrapMdk(mdk: ByteArray): Boolean {
        this.mdk = mdk.copyOf()
        mdk.fill(0)
        return true
    }

    override fun unwrapMdk(): ByteArray? {
        if (simulateCorruption) return null
        return mdk?.copyOf()
    }

    override fun saveCreds(username: String, password: CharArray): Boolean {
        creds = username to password.copyOf()
        password.fill('\u0000')
        return true
    }

    override fun loadCreds(): Pair<String, CharArray>? {
        if (simulateCorruption) return null
        val c = creds ?: return null
        return c.first to c.second.copyOf()
    }

    override fun clear() {
        mdk?.fill(0)
        creds?.second?.fill('\u0000')
        mdk = null
        creds = null
    }
}
