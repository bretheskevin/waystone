package dev.waystone.app.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.DashboardReady
import dev.waystone.app.vm.SaveRow
import dev.waystone.app.vm.UiState
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
    onAddSource: (uri: String, adapter: String, system: String) -> Unit,
    onOpenConflicts: () -> Unit,
    onOpenSettings: () -> Unit,
) {
    val context = LocalContext.current
    var pendingUri by remember { mutableStateOf<Uri?>(null) }

    val treePicker = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        val uri = result.data?.data
        if (result.resultCode == Activity.RESULT_OK && uri != null) {
            runCatching {
                context.contentResolver.takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
                )
            }.onSuccess {
                Log.i(TAG, "persisted URI permission for $uri")
                pendingUri = uri
            }.onFailure {
                Log.e(TAG, "takePersistableUriPermission failed for $uri", it)
            }
        } else {
            Log.i(TAG, "source picker cancelled")
        }
    }

    when (state) {
        is UiState.Loading -> LoadingView()
        is UiState.Error -> ErrorView(state.message)
        is UiState.Ready -> {
            val ready = state.data
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
                    Button(onClick = onSyncAll) { Text("Sync all") }
                    OutlinedButton(onClick = {
                        treePicker.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE))
                    }) { Text("Add source") }
                }

                LazyColumn(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md)) {
                    items(ready.sources, key = { it.folder.uri }) { card ->
                        SourceCardView(
                            card = card,
                            onSyncSource = { onSyncSource(card.folder) },
                            onPush = onPush,
                            onPull = onPull,
                            onOpenHistory = onOpenHistory,
                            onOpenSnapshots = onOpenSnapshots,
                        )
                    }
                }
            }

            pendingUri?.let { uri ->
                AdapterSystemDialog(
                    onConfirm = { adapter, system ->
                        onAddSource(uri.toString(), adapter, system)
                        pendingUri = null
                    },
                    onDismiss = { pendingUri = null },
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AdapterSystemDialog(onConfirm: (adapter: String, system: String) -> Unit, onDismiss: () -> Unit) {
    val adapters = listOf("jksv", "mgba", "checkpoint", "twilight")
    val systems = listOf("switch", "3ds", "nds", "gba", "gbc", "gb")
    var adapter by remember { mutableStateOf(adapters.first()) }
    var system by remember { mutableStateOf(systems.first()) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Source type") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md)) {
                PickerDropdown("Adapter", adapters, adapter) { adapter = it }
                if (adapter != "twilight") {
                    PickerDropdown("System", systems, system) { system = it }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(adapter, if (adapter == "twilight") "" else system) }) { Text("Add") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PickerDropdown(label: String, options: List<String>, selected: String, onSelect: (String) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        androidx.compose.material3.OutlinedTextField(
            value = selected,
            onValueChange = {},
            readOnly = true,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.menuAnchor(),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { option ->
                DropdownMenuItem(text = { Text(option) }, onClick = {
                    onSelect(option)
                    expanded = false
                })
            }
        }
    }
}

@Composable
private fun SourceCardView(
    card: dev.waystone.app.vm.SourceCard,
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
                    Text("${card.folder.adapter} / ${card.folder.system.ifBlank { "auto" }}", style = LocalWaystoneTypography.current.lg)
                    Text(card.folder.uri, style = LocalWaystoneTypography.current.xs)
                }
                if (card.error == null) {
                    StatusBadge(card.worstStatus)
                }
                TextButton(onClick = onSyncSource) { Text("Sync") }
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
            Text(row.mtime, style = LocalWaystoneTypography.current.xs)
        }
        StatusBadge(row.status)
        TextButton(onClick = { onPush(row.groupKey) }) { Text("Push") }
        TextButton(onClick = { onPull(row.groupKey) }) { Text("Pull") }
        TextButton(onClick = { onOpenHistory(row.groupKey) }) { Text("History") }
        TextButton(onClick = { onOpenSnapshots(row.groupKey) }) { Text("Snapshots") }
    }
}
