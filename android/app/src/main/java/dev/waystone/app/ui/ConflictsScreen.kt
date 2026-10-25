package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneColors
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.ConflictInboxState
import dev.waystone.data.sync.ConflictEntry

@Composable
fun ConflictsScreen(
    state: ConflictInboxState,
    onKeepLocal: (String) -> Unit,
    onKeepRemote: (String) -> Unit,
) {
    var pendingRemote by remember { mutableStateOf<ConflictEntry?>(null) }

    Column(
        modifier = Modifier.fillMaxSize().padding(LocalWaystoneSpacing.current.Md),
        verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md),
    ) {
        if (state.scanning) {
            LinearProgressIndicator(
                progress = { if (state.total == 0) 0f else state.scanned.toFloat() / state.total },
                modifier = Modifier.fillMaxWidth(),
            )
            Text("Scanning ${state.scanned}/${state.total}…", style = LocalWaystoneTypography.current.sm)
        }
        state.error?.let { ErrorBanner(it) }
        if (!state.scanning && state.entries.isEmpty()) {
            Text("No conflicts", style = LocalWaystoneTypography.current.base)
        }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md)) {
            items(state.entries, key = { it.save.groupKey }) { entry ->
                val busy = entry.save.groupKey in state.busy
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(LocalWaystoneSpacing.current.Md),
                        verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm),
                    ) {
                        Text(entry.save.displayName, style = LocalWaystoneTypography.current.lg)
                        Text("Changed on ${entry.remoteDeviceId} at ${entry.remoteMtime}", style = LocalWaystoneTypography.current.sm)
                        Text("local ${entry.localHash.take(12)}…  remote ${entry.remoteHash.take(12)}…", style = LocalWaystoneTypography.current.monoSm)
                        if (busy) Text("Resolving…", style = LocalWaystoneTypography.current.sm)
                        Row(horizontalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm)) {
                            TextButton(onClick = { onKeepLocal(entry.save.groupKey) }, enabled = !busy) { Text("Keep local") }
                            TextButton(onClick = { pendingRemote = entry }, enabled = !busy) {
                                Text("Keep remote", color = LocalWaystoneColors.current.warning)
                            }
                        }
                    }
                }
            }
        }
    }

    pendingRemote?.let { entry ->
        AlertDialog(
            onDismissRequest = { pendingRemote = null },
            title = { Text("Overwrite local save?") },
            text = {
                Text(
                    "Keeping the remote version of ${entry.save.displayName} will overwrite the local copy" +
                        " (a safety snapshot is taken first when enabled).",
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    onKeepRemote(entry.save.groupKey)
                    pendingRemote = null
                }) { Text("Keep remote", color = LocalWaystoneColors.current.warning) }
            },
            dismissButton = { TextButton(onClick = { pendingRemote = null }) { Text("Cancel") } },
        )
    }
}
