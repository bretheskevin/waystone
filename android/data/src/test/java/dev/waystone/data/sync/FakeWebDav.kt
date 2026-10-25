package dev.waystone.data.sync

import uniffi.waystone_mobile.WebDav

/** In-memory WebDAV mirroring mobile/tests/mock_sync.rs; PROPFIND lists direct children incl. implied dirs. */
class FakeWebDav : WebDav {
    private val store = mutableMapOf<String, ByteArray>()
    val propfinds = mutableListOf<String>()

    fun keys(): Set<String> = store.keys.toSet()

    override fun `get`(path: String): ByteArray? = store[path]?.copyOf()

    override fun `put`(path: String, body: ByteArray) {
        store[path] = body.copyOf()
    }

    override fun `exists`(path: String): Boolean = store.containsKey(path)

    override fun `propfind`(path: String): List<String> {
        propfinds += path
        val prefix = if (path.endsWith("/")) path else "$path/"
        return store.keys.filter { it.startsWith(prefix) }
            .map { key ->
                val rest = key.substring(prefix.length)
                val slash = rest.indexOf('/')
                if (slash < 0) key else prefix + rest.substring(0, slash + 1)
            }
            .distinct().sorted()
    }

    override fun `mkdirP`(path: String) = Unit
}
