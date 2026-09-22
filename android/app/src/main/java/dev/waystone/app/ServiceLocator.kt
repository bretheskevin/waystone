package dev.waystone.app

import android.content.Context
import android.net.Uri
import android.util.Log
import dev.waystone.data.config.Settings
import dev.waystone.data.config.SettingsStore
import dev.waystone.data.dav.OkHttpWebDav
import dev.waystone.data.fs.SafTree
import dev.waystone.data.session.KeystoreVaultStore
import dev.waystone.data.session.SessionStore
import dev.waystone.data.snapshot.SnapshotStore
import dev.waystone.app.vm.DavFactory
import dev.waystone.app.vm.DavProvider
import dev.waystone.app.vm.SnapshotInfo
import dev.waystone.app.vm.SnapshotStoreProvider
import dev.waystone.app.vm.SaveSourceProvider
import dev.waystone.app.vm.SessionVaultStore
import dev.waystone.app.vm.SettingsProvider
import dev.waystone.app.vm.SourceScan
import dev.waystone.data.config.SourceFolder
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.RawTree
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.checkpointNormalize
import uniffi.waystone_mobile.jksvNormalize
import uniffi.waystone_mobile.mgbaNormalize
import uniffi.waystone_mobile.twilightNormalize

object ServiceLocator {

    lateinit var settingsStore: SettingsStore
        private set
    lateinit var sessionStore: SessionStore
        private set
    lateinit var settingsProvider: SettingsProvider
        private set
    lateinit var sessionVaultStore: SessionVaultStore
        private set
    lateinit var davFactory: DavFactory
        private set
    lateinit var davProvider: DavProvider
        private set
    lateinit var saveSourceProvider: SaveSourceProvider
        private set
    lateinit var snapshotStoreProvider: SnapshotStoreProvider
        private set

    fun init(context: Context) {
        val appContext = context.applicationContext
        settingsStore = SettingsStore(appContext)
        sessionStore = KeystoreVaultStore(appContext)
        val backupsDir = File(appContext.filesDir, "backups")
        settingsProvider = object : SettingsProvider {
            override suspend fun load(): Settings = settingsStore.load()
            override suspend fun save(settings: Settings) = settingsStore.save(settings)
        }
        sessionVaultStore = SessionVaultStoreBinding(sessionStore)
        davFactory = object : DavFactory {
            override suspend fun create(serverUrl: String, username: String, password: String): WebDav =
                withContext(Dispatchers.IO) { OkHttpWebDav(serverUrl.trim(), username.ifBlank { null }, password) }
        }
        davProvider = object : DavProvider {
            override suspend fun current(): WebDav? = withContext(Dispatchers.IO) {
                val settings = settingsStore.load()
                if (settings.serverUrl.isBlank()) return@withContext null
                val (username, password) = sessionStore.loadCreds() ?: return@withContext null
                try {
                    OkHttpWebDav(settings.serverUrl.trim(), username.ifBlank { null }, String(password))
                } finally {
                    password.fill('\u0000')
                }
            }
        }
        saveSourceProvider = SaveSourceProviderBinding(appContext)
        snapshotStoreProvider = SnapshotStoreBinding(backupsDir)
        Log.i(TAG, "ServiceLocator initialized")
    }

    private const val TAG = "WS:APP"
}

private class SessionVaultStoreBinding(private val store: SessionStore) : SessionVaultStore {
    override suspend fun unwrapMdk(): ByteArray? = withContext(Dispatchers.IO) { store.unwrapMdk() }
    override suspend fun wrapMdk(mdk: ByteArray): Boolean = withContext(Dispatchers.IO) { store.wrapMdk(mdk) }
    override suspend fun loadCreds(): Pair<String, CharArray>? = withContext(Dispatchers.IO) { store.loadCreds() }
    override suspend fun saveCreds(username: String, password: CharArray): Boolean =
        withContext(Dispatchers.IO) { store.saveCreds(username, password) }
    override suspend fun clear() = withContext(Dispatchers.IO) { store.clear() }
}

private class SaveSourceProviderBinding(private val context: Context) : SaveSourceProvider {

    override suspend fun scanSources(settings: Settings): List<SourceScan> = withContext(Dispatchers.IO) {
        settings.sources.map { folder ->
            runCatching {
                val saves = normalize(folder.adapter, folder.system, SafTree(context, Uri.parse(folder.uri)).toRawTree())
                SourceScan(folder, saves)
            }.getOrElse { e ->
                // Empty/missing folder is a zero-save source, not an error; only real failures land here.
                Log.w(TAG, "source scan failed: ${folder.uri}", e)
                SourceScan(folder, emptyList(), e.message ?: "Source folder is unavailable")
            }
        }
    }

    override suspend fun findSave(
        settings: Settings,
        groupKey: String,
    ): Pair<SourceFolder, NormalizedSave>? = scanSources(settings)
        .firstNotNullOfOrNull { scan -> scan.saves.firstOrNull { it.groupKey == groupKey }?.let { scan.folder to it } }

    override fun writeFiles(folder: SourceFolder, files: List<FileEntry>): Int =
        SafTree(context, Uri.parse(folder.uri)).writeFiles(files)

    private fun normalize(adapter: String, system: String, raw: RawTree): List<NormalizedSave> = when (adapter) {
        "jksv" -> jksvNormalize(system, raw)
        "mgba" -> mgbaNormalize(system, raw)
        "checkpoint" -> checkpointNormalize(system, raw)
        "twilight" -> twilightNormalize(raw)
        else -> throw IllegalArgumentException("unknown adapter: $adapter")
    }

    private companion object {
        const val TAG = "WS:SOURCES"
    }
}

private class SnapshotStoreBinding(private val root: File) : SnapshotStoreProvider {

    private val store = SnapshotStore(root)

    override suspend fun listSnapshots(groupKey: String): List<SnapshotInfo> = withContext(Dispatchers.IO) {
        store.list(groupKey).map { SnapshotInfo(id = it.name, groupKey = groupKey, timestamp = it.name) }
    }

    override suspend fun readSnapshot(groupKey: String, snapshotId: String): List<FileEntry> =
        withContext(Dispatchers.IO) {
            val dir = File(File(root, SnapshotStore.sanitizeKey(groupKey)), snapshotId)
            if (!SnapshotStore.isTsDirName(snapshotId) || !dir.isDirectory) {
                throw IllegalStateException("unknown snapshot: $groupKey/$snapshotId")
            }
            dir.walkTopDown().filter { it.isFile }
                .map { FileEntry(it.relativeTo(dir).invariantSeparatorsPath, it.readBytes()) }
                .toList()
        }

    override suspend fun snapshot(groupKey: String, files: Map<String, ByteArray>): Boolean =
        withContext(Dispatchers.IO) { store.snapshot(groupKey, files) }

    override suspend fun prune(groupKey: String) = withContext(Dispatchers.IO) { store.prune(groupKey) }

    private companion object {
        const val TAG = "WS:SNAPSHOTS"
    }
}
