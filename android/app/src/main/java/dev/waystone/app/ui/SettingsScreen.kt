package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.PasswordVisualTransformation
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.SettingsReady
import dev.waystone.app.vm.UiState

@Composable
fun SettingsScreen(
    state: UiState<SettingsReady>,
    onChangeServer: (serverUrl: String, username: String, password: String) -> Unit,
    onPolicyChange: (String) -> Unit,
    onSafetyBackupChange: (Boolean) -> Unit,
    onLogout: () -> Unit,
) {
    when (state) {
        is UiState.Loading -> LoadingView()
        is UiState.Error -> ErrorView(state.message)
        is UiState.Ready -> {
            val ready = state.data
            var serverUrl by remember(ready.serverUrl) { mutableStateOf(ready.serverUrl) }
            var username by remember(ready.username) { mutableStateOf(ready.username) }
            var password by remember { mutableStateOf("") }

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(LocalWaystoneSpacing.current.Lg),
                verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md),
            ) {
                Text("Settings", style = LocalWaystoneTypography.current.xxxl)

                OutlinedTextField(
                    value = serverUrl,
                    onValueChange = { serverUrl = it },
                    label = { Text("Server URL") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = username,
                    onValueChange = { username = it },
                    label = { Text("Username") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = password,
                    onValueChange = { password = it },
                    label = { Text("New WebDAV password (optional)") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    modifier = Modifier.fillMaxWidth(),
                )
                Button(
                    onClick = { onChangeServer(serverUrl, username, password) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Save server") }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text("Conflict policy", style = LocalWaystoneTypography.current.base)
                        Text(
                            if (ready.conflictPolicy == "prompt") "Prompt me per conflict" else "Newest wins automatically",
                            style = LocalWaystoneTypography.current.sm,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Switch(
                        checked = ready.conflictPolicy == "prompt",
                        onCheckedChange = { onPolicyChange(if (it) "prompt" else "newest-wins") },
                    )
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text("Safety backup", style = LocalWaystoneTypography.current.base)
                        Text(
                            "Snapshot before every restore",
                            style = LocalWaystoneTypography.current.sm,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Switch(checked = ready.safetyBackup, onCheckedChange = onSafetyBackupChange)
                }

                Text("Device ID", style = LocalWaystoneTypography.current.sm)
                Text(ready.deviceId, style = LocalWaystoneTypography.current.monoSm)

                OutlinedButton(onClick = onLogout, modifier = Modifier.fillMaxWidth()) {
                    Text("Log out")
                }
            }
        }
    }
}
