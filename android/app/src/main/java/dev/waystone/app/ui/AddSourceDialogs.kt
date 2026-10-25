package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.data.config.EmulatorPreset
import dev.waystone.data.config.ROM_KEYED_ADAPTER

@Composable
internal fun PresetChooserDialog(onChoose: (EmulatorPreset?) -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add a source") },
        text = {
            Column {
                EmulatorPreset.entries.forEach { preset ->
                    TextButton(onClick = { onChoose(preset) }) { Text(preset.label) }
                }
                TextButton(onClick = { onChoose(null) }) { Text("Manual (JKSV / mGBA / Checkpoint / ROM folder)") }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

private fun savesHelp(preset: EmulatorPreset): String = when (preset) {
    EmulatorPreset.RETROARCH ->
        "Pick the folder RetroArch saves to (Settings > Directory > Save Files), e.g. RetroArch/saves. " +
            "If 'Sort saves by core' is on, pick the parent folder. Play Store builds that keep saves under " +
            "Android/data cannot be shared with other apps: point RetroArch's save directory to a shared folder first."
    EmulatorPreset.DRASTIC -> "Pick DraStic's backup folder (DraStic/backup) where its .dsv saves live."
    else -> "Pick the folder where ${preset.label} keeps its saves."
}

@Composable
internal fun ExplainSavesDialog(preset: EmulatorPreset, onPick: () -> Unit, onSkip: () -> Unit) {
    AlertDialog(
        onDismissRequest = onSkip,
        title = { Text("${preset.label} saves folder") },
        text = { Text(savesHelp(preset)) },
        confirmButton = { TextButton(onClick = onPick) { Text("Pick saves folder") } },
        dismissButton = { TextButton(onClick = onSkip) { Text("Skip") } },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun AdapterSystemDialog(onConfirm: (adapter: String, system: String) -> Unit, onDismiss: () -> Unit) {
    val adapters = listOf("jksv", "mgba", "checkpoint", ROM_KEYED_ADAPTER)
    val systems = listOf("switch", "3ds", "nds", "gba", "gbc", "gb")
    var adapter by remember { mutableStateOf(adapters.first()) }
    var system by remember { mutableStateOf(systems.first()) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Source type") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md)) {
                PickerDropdown("Adapter", adapters, adapter) { adapter = it }
                if (adapter != ROM_KEYED_ADAPTER) {
                    PickerDropdown("System", systems, system) { system = it }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(adapter, system) }) { Text("Add") }
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
