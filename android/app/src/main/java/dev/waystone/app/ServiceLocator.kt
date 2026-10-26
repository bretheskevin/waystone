package dev.waystone.app

import android.content.Context
import android.net.Uri
import dev.waystone.app.log.Log
import dev.waystone.data.config.Settings
import dev.waystone.app.log.LogShare
import dev.waystone.data.log.WsLogFile
import dev.waystone.data.config.SettingsStore
import dev.waystone.data.dav.OkHttpWebDav
import dev.waystone.data.fs.SafRomFileSource
import dev.waystone.data.fs.SafTree
import dev.waystone.data.config.ROM_KEYED_ADAPTER
import dev.waystone.data.config.externalFlavor
import dev.waystone.data.roms.CompositeRomFileSource
import dev.waystone.data.roms.LocalRom
import dev.waystone.data.roms.RemoteOnlyProbe
import dev.waystone.data.roms.RomArt
import dev.waystone.data.roms.RomArtLoader
import dev.waystone.data.roms.RestorePlacement
import dev.waystone.data.roms.RestoreTree
import dev.waystone.data.roms.RomFileSource
import dev.waystone.data.roms.RomScanner
import dev.waystone.data.session.KeystoreVaultStore
import dev.waystone.data.session.SessionStore
import dev.waystone.data.snapshot.SnapshotStore
import dev.waystone.app.net.Connectivity
import dev.waystone.app.vm.DavFactory
import dev.waystone.app.vm.DavProvider
import dev.waystone.app.vm.NetworkMonitor
import dev.waystone.app.vm.SnapshotInfo
import dev.waystone.app.vm.SnapshotStoreProvider
import dev.waystone.app.vm.SaveSourceProvider
import dev.waystone.app.vm.SessionVaultStore
import dev.waystone.app.vm.SettingsProvider
import dev.waystone.app.vm.SourceScan
import dev.waystone.data.config.SourceFolder
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import uniffi.waystone_mobile.FileEntry
import uniffi.waystone_mobile.LogSink
import uniffi.waystone_mobile.installLogSink
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.RawTree
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.checkpointNormalize
import uniffi.waystone_mobile.jksvNormalize
import uniffi.waystone_mobile.mgbaNormalize
import uniffi.waystone_mobile.romKeyedToNative

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
    lateinit var networkMonitor: NetworkMonitor
        private set

    fun init(context: Context) {
        val appContext = context.applicationContext
        val logFile = WsLogFile.install(LogShare.logDir(appContext))
        val sinkInstalled = installLogSink(object : LogSink {
            override fun log(level: String, message: String) {
                when (level) {
                    "ERROR" -> Log.e(ENGINE_TAG, message)
                    "WARN" -> Log.w(ENGINE_TAG, message)
                    else -> Log.i(ENGINE_TAG, message)
                }
            }
        })
        Log.i(TAG, "log file at ${logFile.path}; engine log sink ${if (sinkInstalled) "installed" else "already installed"}")
        networkMonitor = Connectivity(appContext)
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
    private const val ENGINE_TAG = "WS:ENGINE"
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

    override suspend fun scanSources(settings: Settings, probe: RemoteOnlyProbe?): List<SourceScan> = withContext(Dispatchers.IO) {
        settings.sources.map { folder ->
            runCatching {
                val saves = if (isRomKeyed(folder.adapter)) {
                    scanRoms(folder, probe)
                } else {
                    normalize(folder.adapter, folder.system, SafTree(context, Uri.parse(folder.uri)).toRawTree())
                }
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

    override fun writeFiles(folder: SourceFolder, save: NormalizedSave, files: List<FileEntry>): Int {
        if (!isRomKeyed(folder.adapter)) return SafTree(context, Uri.parse(folder.uri)).writeFiles(files)
        val rom = localRom(folder, save)
        val scanner = RomScanner(
            romSources[folder.key] ?: romSourceFor(folder).also { it.listPaths(RomScanner.MAX_DEPTH) },
            folder.externalFlavor(),
        )
        val existing = scanner.readSaveFiles(rom)
        val native = romKeyedToNative(save.copy(files = files), rom.romFileName, rom.flavor, existing).files
        if (native.isEmpty() && files.isNotEmpty()) {
            error("[roms] blob for ${save.groupKey} has no file for slot ${save.slot}")
        }
        val placement = RestorePlacement.of(rom.saveDir, native.map { FileEntry(it.path, it.content) })
        val treeUri = when (placement.tree) {
            RestoreTree.Roms -> folder.uri
            RestoreTree.Saves -> folder.saveUri
                ?: error("[roms] ${save.groupKey} targets the save tree but the source has none")
        }
        Log.i(
            TAG,
            "[roms] restoring ${save.groupKey} -> ${placement.tree} ${rom.saveDir} " +
                "(${placement.files.size} file(s), flavor=${rom.flavor}, keepFooterFrom=${existing.size} local file(s))",
        )
        return try {
            SafTree(context, Uri.parse(treeUri)).writeFiles(placement.files).also {
                Log.i(TAG, "[roms] restore done: ${save.groupKey} wrote $it file(s)")
            }
        } catch (e: Exception) {
            Log.e(TAG, "[roms] restore failed: ${save.groupKey}", e)
            throw e
        }
    }

    override suspend fun romArt(folder: SourceFolder, artKey: String): RomArt? = withContext(Dispatchers.IO) {
        if (!isRomKeyed(folder.adapter)) return@withContext null
        val system = artKey.substringBefore('/')
        val rom = runCatching { localRomBy(folder, system, artKey.substringAfter('/')) }
            .onFailure { Log.w(TAG, "art $artKey: no local ROM", it) }
            .getOrNull() ?: return@withContext null
        val roms = safSource(folder.uri)
        val (kind, art) = if (system == "nds") {
            "nds" to RomArtLoader.ndsIcon(roms, rom.romPath)
        } else {
            "boxart" to RomArtLoader.boxArt(listOfNotNull(roms, folder.saveUri?.let(::safSource)), system, rom.romFileName)
        }
        Log.i(TAG, "art $artKey: ${if (art == null) "none" else kind}")
        art
    }

    private val romSources = ConcurrentHashMap<String, RomFileSource>()

    private fun safSource(treeUri: String) = SafRomFileSource(context, Uri.parse(treeUri))

    private fun romSourceFor(folder: SourceFolder): RomFileSource {
        val roms = safSource(folder.uri)
        val source = folder.saveUri?.let { CompositeRomFileSource(roms, safSource(it)) } ?: roms
        romSources[folder.key] = source
        return source
    }

    private val romIndex = ConcurrentHashMap<String, LocalRom>()

    private fun isRomKeyed(adapter: String) = adapter == ROM_KEYED_ADAPTER || adapter == "twilight"

    private fun romKey(folder: SourceFolder, system: String, romId: String) = "${folder.key}|$system/$romId"

    private fun scanRoms(folder: SourceFolder, probe: RemoteOnlyProbe? = null): List<NormalizedSave> {
        Log.i(TAG, "[roms] scanning ${folder.uri}")
        val scanner = RomScanner(romSourceFor(folder), folder.externalFlavor())
        val roms = scanner.scan()
        roms.forEach { romIndex[romKey(folder, it.system, it.romId)] = it }
        val local = scanner.loadSaves(roms)
        val remote = probe?.find(roms, local.map { it.groupKey }.toSet()).orEmpty()
        Log.i(TAG, "[roms] ${folder.uri}: ${roms.size} ROM(s), ${local.size} local save(s), ${remote.size} remote-only")
        return local + remote
    }

    private fun localRom(folder: SourceFolder, save: NormalizedSave): LocalRom =
        localRomBy(folder, save.system, save.gameKey)

    private fun localRomBy(folder: SourceFolder, system: String, romId: String): LocalRom {
        val key = romKey(folder, system, romId)
        romIndex[key]?.let { return it }
        Log.i(TAG, "[roms] index miss for $key, rescanning ${folder.uri}")
        scanRoms(folder)
        return romIndex[key] ?: error("[roms] no local ROM for $system/$romId in ${folder.uri}")
    }

    private fun normalize(adapter: String, system: String, raw: RawTree): List<NormalizedSave> = when (adapter) {
        "jksv" -> jksvNormalize(system, raw)
        "mgba" -> mgbaNormalize(system, raw)
        "checkpoint" -> checkpointNormalize(system, raw)
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
