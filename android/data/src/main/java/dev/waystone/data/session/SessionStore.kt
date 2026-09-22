package dev.waystone.data.session

interface SessionStore {
    /** AES/GCM encrypt + persist the MDK; zeroizes [mdk] after wrapping. false on failure. */
    fun wrapMdk(mdk: ByteArray): Boolean

    /** null when absent/corrupt — caller falls back to passphrase unlock. */
    fun unwrapMdk(): ByteArray?

    /** Encrypt "user\npass" and persist; zeroizes [password]. false on failure. */
    fun saveCreds(username: String, password: CharArray): Boolean

    /** null when absent/corrupt; caller clears the returned CharArray. */
    fun loadCreds(): Pair<String, CharArray>?

    fun clear()
}
