package dev.waystone.app.vm

import dev.waystone.data.sync.runCatchingCancellable
import dev.waystone.data.update.CheckResult
import dev.waystone.data.update.ReleaseInfo
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import java.util.logging.Logger
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

sealed interface UpdatePhase {
    data object Idle : UpdatePhase
    data object Checking : UpdatePhase
    data class UpToDate(val latest: String) : UpdatePhase
    data class Available(val release: ReleaseInfo) : UpdatePhase
    data class Downloading(val percent: Int) : UpdatePhase
    data object NeedsPermission : UpdatePhase
    data object Installing : UpdatePhase
    data class Error(val message: String) : UpdatePhase
}

/** Update phase machine (console parity: Idle/Checking/Downloading/Installing/Done/Error, user-triggered). */
class UpdateController(
    private val scope: CoroutineScope,
    private val currentVersion: String,
    private val check: suspend (String) -> CheckResult,
    private val download: suspend (url: String, onProgress: (Long, Long) -> Unit, isCancelled: () -> Boolean) -> File?,
    private val canInstall: () -> Boolean,
    private val install: (File) -> Unit,
    private val ioContext: CoroutineContext = Dispatchers.IO,
) {
    private val log = Logger.getLogger("WS:UPDATE")
    private val _phase = MutableStateFlow<UpdatePhase>(UpdatePhase.Idle)
    val phase: StateFlow<UpdatePhase> = _phase.asStateFlow()
    private val cancelled = AtomicBoolean(false)
    private var available: ReleaseInfo? = null

    fun check() {
        if (_phase.value is UpdatePhase.Checking || _phase.value is UpdatePhase.Downloading) return
        _phase.value = UpdatePhase.Checking
        log.info("user check: current v$currentVersion")
        scope.launch {
            _phase.value = when (val r = withContext(ioContext) { check(currentVersion) }) {
                is CheckResult.UpToDate -> UpdatePhase.UpToDate(r.latest)
                is CheckResult.Available -> UpdatePhase.Available(r.release).also { available = r.release }
                is CheckResult.Failed -> UpdatePhase.Error(r.message)
            }
            log.info("check result: ${_phase.value}")
        }
    }

    fun downloadAndInstall() {
        val release = available ?: return
        if (!canInstall()) {
            log.info("install permission missing (REQUEST_INSTALL_PACKAGES not granted by user)")
            _phase.value = UpdatePhase.NeedsPermission
            return
        }
        cancelled.set(false)
        _phase.value = UpdatePhase.Downloading(0)
        scope.launch {
            val file = withContext(ioContext) {
                download(release.assetUrl, { done, total ->
                    if (total > 0) _phase.value = UpdatePhase.Downloading(((done * 100) / total).toInt().coerceIn(0, 100))
                }, cancelled::get)
            }
            when {
                cancelled.get() -> _phase.value = UpdatePhase.Available(release).also { log.info("update download cancelled") }
                file == null -> _phase.value = UpdatePhase.Error("Download failed")
                else -> {
                    _phase.value = UpdatePhase.Installing
                    runCatchingCancellable { withContext(ioContext) { install(file) } }
                        .onSuccess { log.info("install session committed for v${release.version}") }
                        .onFailure { e ->
                            log.warning("install session failed: ${e.message}")
                            _phase.value = UpdatePhase.Error("Install failed: ${e.message ?: "session error"}")
                        }
                }
            }
        }
    }

    fun cancel() {
        cancelled.set(true)
        log.info("update cancel requested")
    }

    fun onInstallResult(success: Boolean, message: String?) {
        log.info("install result: success=$success message=$message")
        _phase.value = if (success) UpdatePhase.Idle else UpdatePhase.Error("Install failed: ${message ?: "unknown"}")
    }
}
