package dev.waystone.app.vm

import dev.waystone.data.update.CheckResult
import dev.waystone.data.update.ReleaseInfo
import java.io.File
import kotlin.coroutines.EmptyCoroutineContext
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class UpdateControllerTest {
    private val rel = ReleaseInfo("0.6.0", "https://x/waystone.apk")

    @Test
    fun `check then install happy path`() = runTest {
        val installed = mutableListOf<File>()
        val c = UpdateController(this, "0.5.4",
            check = { CheckResult.Available(rel) },
            download = { _, onProgress, _ -> onProgress(50, 100); File("update.apk") },
            canInstall = { true }, install = { installed += it }, ioContext = EmptyCoroutineContext)
        c.check(); advanceUntilIdle()
        assertEquals(UpdatePhase.Available(rel), c.phase.value)
        c.downloadAndInstall(); advanceUntilIdle()
        assertEquals(UpdatePhase.Installing, c.phase.value)
        assertEquals(listOf(File("update.apk")), installed)
        c.onInstallResult(success = false, message = "INSTALL_FAILED_UPDATE_INCOMPATIBLE")
        assertEquals(UpdatePhase.Error("Install failed: INSTALL_FAILED_UPDATE_INCOMPATIBLE"), c.phase.value)
    }

    @Test
    fun `up to date, failure, missing permission, failed download`() = runTest {
        var result: CheckResult = CheckResult.UpToDate("0.5.4")
        var allowed = false
        val c = UpdateController(this, "0.5.4", check = { result },
            download = { _, _, _ -> null }, canInstall = { allowed }, install = {}, ioContext = EmptyCoroutineContext)
        c.check(); advanceUntilIdle()
        assertEquals(UpdatePhase.UpToDate("0.5.4"), c.phase.value)
        result = CheckResult.Failed("GitHub returned HTTP 403"); c.check(); advanceUntilIdle()
        assertEquals(UpdatePhase.Error("GitHub returned HTTP 403"), c.phase.value)
        result = CheckResult.Available(rel); c.check(); advanceUntilIdle()
        c.downloadAndInstall(); advanceUntilIdle()
        assertEquals(UpdatePhase.NeedsPermission, c.phase.value)
        allowed = true; c.downloadAndInstall(); advanceUntilIdle()
        assertEquals(UpdatePhase.Error("Download failed"), c.phase.value)
    }
}
