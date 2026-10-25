package dev.waystone.app.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.provider.DocumentsContract
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.ActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.AddSourceFlow
import dev.waystone.app.vm.AddSourceStep
import dev.waystone.app.vm.DashboardReady
import dev.waystone.app.vm.SaveRow
import dev.waystone.app.vm.SyncRunState
import dev.waystone.app.vm.UiState
import dev.waystone.data.config.EmulatorPreset
import dev.waystone.data.config.SourceFolder

private const val TAG = "WS:DASHBOARD"

@Composable
fun DashboardScreen(
    state: UiState<DashboardReady>,
    onRefresh: () -> Unit,
    onSyncAll: () -> Unit,
    onSyncSource: (SourceFolder) -> Unit,
    onPush: (String) -> Unit,
    onPull: (String) -> Unit,
    onOpenHistory: (String) -> Unit,
    onOpenSnapshots: (String) -> Unit,
    onAddSource: (SourceFolder) -> Unit,
    onOpenConflicts: () -> Unit,
    onOpenSettings: () -> Unit,
    syncRun: SyncRunState?,
    onCancelSync: () -> Unit,
    onCloseSync: () -> Unit,
) {
    val context = LocalContext.current
    var step by remember { mutableStateOf<AddSourceStep>(AddSourceStep.Idle) }

    fun advance(next: AddSourceStep) {
        Log.i(TAG, "add source: $next")
        step = next
    }

    fun persisted(result: ActivityResult): String? {
        val uri = result.data?.data
        if (result.resultCode != Activity.RESULT_OK || uri == null) {
            Log.i(TAG, "source picker cancelled")
            return null
        }
        return runCatching {
            context.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
        }.onSuccess { Log.i(TAG, "persisted URI permission for $uri") }
            .onFailure { Log.e(TAG, "takePersistableUriPermission failed for $uri", it) }
            .map { uri.toString() }
            .getOrNull()
    }

    val romsPicker = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        (step as? AddSourceStep.PickRoms)?.let { advance(AddSourceFlow.onRomsPicked(it, persisted(result))) }
    }
    val savesPicker = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        (step as? AddSourceStep.PickSaves)?.let { advance(AddSourceFlow.onSavesPicked(it, persisted(result))) }
    }

    LaunchedEffect(step) {
        when (val s = step) {
            is AddSourceStep.PickRoms -> romsPicker.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE))
            is AddSourceStep.PickSaves -> savesPicker.launch(
                Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
                    s.preset.savesInitialDocId?.let {
                        putExtra(
                            DocumentsContract.EXTRA_INITIAL_URI,
                            DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", it),
                        )
                    }
                },
            )
            is AddSourceStep.Done -> {
                onAddSource(s.folder)
                advance(AddSourceStep.Idle)
            }
            else -> Unit
        }
    }

    when (state) {
        is UiState.Loading -> LoadingView()
        is UiState.Error -> ErrorView(state.message)
        is UiState.Ready -> {
            val ready = state.data
            val idle = syncRun?.running != true
            Column(modifier = Modifier.fillMaxSize().padding(LocalWaystoneSpacing.current.Md)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("Waystone", style = LocalWaystoneTypography.current.xxl, modifier = Modifier.weight(1f))
                    if (ready.conflictCount > 0) {
                        OutlinedButton(onClick = onOpenConflicts) {
                            Text("Conflicts (${ready.conflictCount})")
                        }
                    }
                    TextButton(onClick = onOpenSettings) { Text("Settings") }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm),
                ) {
                    Button(onClick = onSyncAll, enabled = idle) { Text("Sync all") }
                    OutlinedButton(onClick = { advance(AddSourceStep.ChoosePreset) }) { Text("Add source") }
                }

                LazyColumn(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md)) {
                    items(ready.sources, key = { it.folder.key }) { card ->
                        SourceCardView(
                            card = card,
                            syncEnabled = idle,
                            onSyncSource = { onSyncSource(card.folder) },
                            onPush = onPush,
                            onPull = onPull,
                            onOpenHistory = onOpenHistory,
                            onOpenSnapshots = onOpenSnapshots,
                        )
                    }
                }
            }

            syncRun?.let { SyncProgressDialog(it, onCancelSync, onCloseSync) }

            when (val current = step) {
                is AddSourceStep.ChoosePreset -> PresetChooserDialog(
                    onChoose = { advance(AddSourceFlow.onPresetChosen(it)) },
                    onDismiss = { advance(AddSourceStep.Idle) },
                )
                is AddSourceStep.ConfirmManual -> AdapterSystemDialog(
                    onConfirm = { adapter, system -> advance(AddSourceFlow.onManualConfirmed(current, adapter, system)) },
                    onDismiss = { advance(AddSourceStep.Idle) },
                )
                is AddSourceStep.ExplainSaves -> ExplainSavesDialog(
                    preset = current.preset,
                    onPick = { advance(AddSourceFlow.onExplained(current, pickSaves = true)) },
                    onSkip = { advance(AddSourceFlow.onExplained(current, pickSaves = false)) },
                )
                else -> Unit
            }
        }
    }
}

@Composable
private fun SourceCardView(
    card: dev.waystone.app.vm.SourceCard,
    syncEnabled: Boolean,
    onSyncSource: () -> Unit,
    onPush: (String) -> Unit,
    onPull: (String) -> Unit,
    onOpenHistory: (String) -> Unit,
    onOpenSnapshots: (String) -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(LocalWaystoneSpacing.current.Md),
            verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        EmulatorPreset.of(card.folder.preset)?.label
                            ?: "${card.folder.adapter} / ${card.folder.system.ifBlank { "auto" }}",
                        style = LocalWaystoneTypography.current.lg,
                    )
                    Text(card.folder.uri, style = LocalWaystoneTypography.current.xs)
                    card.folder.saveUri?.let { Text("Saves: $it", style = LocalWaystoneTypography.current.xs) }
                }
                if (card.error == null) {
                    StatusBadge(card.worstStatus)
                }
                TextButton(onClick = onSyncSource, enabled = syncEnabled) { Text("Sync") }
            }
            card.error?.let { ErrorBanner(it) }
            card.saves.forEach { row -> SaveRowView(row, onPush, onPull, onOpenHistory, onOpenSnapshots) }
            if (card.saves.isEmpty() && card.error == null) {
                Text("No saves found", style = LocalWaystoneTypography.current.sm)
            }
        }
    }
}

@Composable
private fun SaveRowView(
    row: SaveRow,
    onPush: (String) -> Unit,
    onPull: (String) -> Unit,
    onOpenHistory: (String) -> Unit,
    onOpenSnapshots: (String) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Column(modifier = Modifier.weight(1f)) {
            Text(row.displayName, style = LocalWaystoneTypography.current.base)
            if (row.remoteOnly) Text("Remote only — tap Pull to restore", style = LocalWaystoneTypography.current.xs)
            else Text(row.mtime, style = LocalWaystoneTypography.current.xs)
        }
        StatusBadge(row.status)
        if (!row.remoteOnly) TextButton(onClick = { onPush(row.groupKey) }) { Text("Push") }
        TextButton(onClick = { onPull(row.groupKey) }) { Text("Pull") }
        if (!row.remoteOnly) {
            TextButton(onClick = { onOpenHistory(row.groupKey) }) { Text("History") }
            TextButton(onClick = { onOpenSnapshots(row.groupKey) }) { Text("Snapshots") }
        }
    }
}
