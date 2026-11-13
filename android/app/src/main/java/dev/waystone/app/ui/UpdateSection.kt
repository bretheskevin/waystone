package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.vm.UpdatePhase
import dev.waystone.app.vm.UpdateViewModel

/** Binds the shared activity-scoped [UpdateViewModel]; Settings, Unlock and Setup all render this. */
@Composable
fun UpdateSection(vm: UpdateViewModel) {
    val phase by vm.phase.collectAsState()
    UpdateSection(phase, vm::check, vm::downloadAndInstall, vm::cancel, vm::openInstallPermissionSettings)
}

@Composable
fun UpdateSection(
    phase: UpdatePhase,
    onCheck: () -> Unit,
    onInstall: () -> Unit,
    onCancel: () -> Unit,
    onOpenPermission: () -> Unit,
) {
    val checkButton = @Composable {
        OutlinedButton(onClick = onCheck, modifier = Modifier.fillMaxWidth()) { Text("Check for updates") }
    }
    Column(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm)) {
        when (phase) {
            UpdatePhase.Idle -> checkButton()
            UpdatePhase.Checking -> Text("Checking\u2026")
            is UpdatePhase.UpToDate -> {
                Text("Up to date (latest v${phase.latest})")
                checkButton()
            }
            is UpdatePhase.Available -> {
                Text("Version ${phase.release.version} available")
                Button(onClick = onInstall, modifier = Modifier.fillMaxWidth()) { Text("Download & install") }
            }
            is UpdatePhase.Downloading -> {
                LinearProgressIndicator(progress = { phase.percent / 100f }, modifier = Modifier.fillMaxWidth())
                Text("Downloading\u2026 ${phase.percent}%")
                TextButton(onClick = onCancel) { Text("Cancel") }
            }
            UpdatePhase.NeedsPermission -> {
                Text("Allow Waystone to install updates: Android asks once per app.")
                Button(onClick = onOpenPermission, modifier = Modifier.fillMaxWidth()) { Text("Open settings") }
                TextButton(onClick = onInstall) { Text("Retry") }
            }
            UpdatePhase.Installing -> Text("Installing\u2026 confirm the system prompt")
            is UpdatePhase.Error -> {
                Text(phase.message, color = MaterialTheme.colorScheme.error)
                checkButton()
            }
        }
    }
}
