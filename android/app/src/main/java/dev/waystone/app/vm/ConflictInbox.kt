package dev.waystone.app.vm

import dev.waystone.data.sync.ConflictEntry
import dev.waystone.data.sync.runCatchingCancellable
import java.util.logging.Level
import java.util.logging.Logger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import uniffi.waystone_mobile.NormalizedSave

data class ConflictInboxState(
    val entries: List<ConflictEntry> = emptyList(),
    val scanning: Boolean = false,
    val scanned: Int = 0,
    val total: Int = 0,
    val busy: Set<String> = emptySet(),
    val error: String? = null,
    /** True once a scan has run to the end (success or failure). */
    val completed: Boolean = false,
) {
    val finishedEmpty: Boolean get() = completed && !scanning && entries.isEmpty() && busy.isEmpty()
}

/** Progressive conflict scan + FIFO resolve queue (mirrors shell-common/conflict_core.h). */
class ConflictInbox(
    private val scope: CoroutineScope,
    private val loadSaves: suspend () -> List<NormalizedSave>,
    private val scanOne: suspend (NormalizedSave) -> ConflictEntry?,
    private val onKeepLocal: suspend (ConflictEntry) -> Unit,
    private val onKeepRemote: suspend (ConflictEntry) -> Unit,
) {
    private val log = Logger.getLogger("WS:CONFLICTS")
    private val _state = MutableStateFlow(ConflictInboxState())
    val state: StateFlow<ConflictInboxState> = _state.asStateFlow()
    private val queue = Mutex()
    private var scanJob: Job? = null

    fun startScan() {
        scanJob?.cancel()
        _state.value = ConflictInboxState(scanning = true)
        log.info("conflict scan start")
        scanJob = scope.launch {
            try {
                runCatchingCancellable {
                    val saves = loadSaves()
                    _state.update { it.copy(total = saves.size) }
                    for (save in saves) {
                        val entry = runCatchingCancellable { scanOne(save) }
                            .onFailure { e -> log.log(Level.WARNING, "conflict scan failed for ${save.groupKey}", e) }
                            .getOrNull()
                        // groupKey is the list key: the same game scanned from a second source is listed once.
                        val duplicate = entry != null && _state.value.entries.any { it.save.groupKey == save.groupKey }
                        if (duplicate) log.info("conflict ${save.groupKey} already listed from another source")
                        val added = entry.takeUnless { duplicate }
                        _state.update { s -> s.copy(scanned = s.scanned + 1, entries = if (added != null) s.entries + added else s.entries) }
                        if (added != null) log.info("conflict found: ${save.groupKey}")
                    }
                    log.info("conflict scan done: ${_state.value.entries.size} conflict(s) in ${saves.size} save(s)")
                }.onFailure { e ->
                    log.log(Level.WARNING, "conflict scan failed", e)
                    _state.update { it.copy(error = e.message ?: "Conflict scan failed") }
                }
            } finally {
                _state.update { it.copy(scanning = false, completed = true) }
            }
        }
    }

    fun resolve(groupKey: String, keepRemote: Boolean) {
        val current = _state.value
        val entry = current.entries.firstOrNull { it.save.groupKey == groupKey }
        if (entry == null || groupKey in current.busy) {
            log.info("resolve ignored for $groupKey (missing or already queued)")
            return
        }
        _state.update { it.copy(busy = it.busy + groupKey, error = null) }
        val how = if (keepRemote) "keep-remote" else "keep-local"
        log.info("resolve queued: $groupKey ($how)")
        scope.launch {
            queue.withLock {
                try {
                    runCatchingCancellable { if (keepRemote) onKeepRemote(entry) else onKeepLocal(entry) }
                        .onSuccess {
                            _state.update { s -> s.copy(entries = s.entries.filterNot { it.save.groupKey == groupKey }) }
                            log.info("resolved $groupKey ($how)")
                        }
                        .onFailure { e ->
                            log.log(Level.WARNING, "resolve failed for $groupKey ($how)", e)
                            _state.update { it.copy(error = "${entry.save.displayName}: ${e.message ?: "$how failed"}") }
                        }
                } finally {
                    _state.update { it.copy(busy = it.busy - groupKey) }
                }
            }
        }
    }
}
