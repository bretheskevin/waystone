package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.window.DialogProperties
import dev.waystone.app.ui.theme.LocalWaystoneColors
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.SyncRunState
import dev.waystone.data.sync.SyncPhase

/** Back while running = cancel after the current save; Back/Close after Done = dismiss. */
@Composable
fun SyncProgressDialog(run: SyncRunState, onCancel: () -> Unit, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = { if (run.running) onCancel() else onClose() },
        properties = DialogProperties(dismissOnBackPress = true, dismissOnClickOutside = false),
        title = { Text(if (run.running) "Syncing…" else "Sync finished") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm)) {
                val summary = run.summary
                if (summary == null) {
                    LinearProgressIndicator(progress = { run.fraction }, modifier = Modifier.fillMaxWidth())
                    Text("${run.position} / ${run.total}", style = LocalWaystoneTypography.current.sm)
                    run.currentName?.let { name ->
                        Text("${phaseLabel(run.phase)} $name", style = LocalWaystoneTypography.current.base)
                    }
                    if (run.cancelling) Text("Stopping after the current save…", style = LocalWaystoneTypography.current.sm)
                } else {
                    Text(summary.headline(), style = LocalWaystoneTypography.current.base)
                    summary.errorMessages.forEach {
                        Text(it, style = LocalWaystoneTypography.current.sm, color = LocalWaystoneColors.current.error)
                    }
                    if (summary.conflicts > 0) Text("Resolve conflicts from the Conflicts inbox.", style = LocalWaystoneTypography.current.sm)
                }
            }
        },
        confirmButton = {
            if (run.running) TextButton(onClick = onCancel, enabled = !run.cancelling) { Text("Cancel") }
            else TextButton(onClick = onClose) { Text("Close") }
        },
    )
}

private fun phaseLabel(phase: SyncPhase?): String = when (phase) {
    SyncPhase.Pushing -> "Uploading"
    SyncPhase.Pulling -> "Downloading"
    SyncPhase.Restoring -> "Restoring"
    null -> ""
}
