package dev.waystone.app.vm

import uniffi.waystone_mobile.WaystoneException
import uniffi.waystone_mobile.WebDav

internal class FakeSessionVaultStore(
    var mdk: ByteArray? = null,
    var creds: Pair<String, CharArray>? = null,
) : SessionVaultStore {
    val savedCreds = mutableListOf<Pair<String, CharArray>>()
    var wrapCount = 0
    var clearCount = 0

    override suspend fun unwrapMdk(): ByteArray? = mdk
    override suspend fun wrapMdk(mdk: ByteArray): Boolean {
        wrapCount++
        mdk.fill(0)
        return true
    }

    override suspend fun loadCreds(): Pair<String, CharArray>? = creds
    override suspend fun saveCreds(username: String, password: CharArray): Boolean {
        savedCreds += username to password.copyOf()
        password.fill('\u0000')
        return true
    }

    override suspend fun clear() {
        clearCount++
        mdk = null
        creds = null
    }
}

internal class FakeWebDav(
    private val keysJson: ByteArray? = null,
    private val rejectGet: Boolean = false,
) : WebDav {
    val puts = mutableListOf<Pair<String, ByteArray>>()

    override fun get(path: String): ByteArray? {
        if (rejectGet) throw WaystoneException.WebDav("GET $path returned 401")
        return keysJson
    }

    override fun put(path: String, body: ByteArray) {
        puts += path to body
    }

    override fun exists(path: String): Boolean = true
    override fun propfind(path: String): List<String> = emptyList()
    override fun mkdirP(path: String) {}
}
