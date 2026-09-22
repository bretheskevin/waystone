package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.input.PasswordVisualTransformation
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.SetupReady
import dev.waystone.app.vm.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SetupScreen(
    state: UiState<SetupReady>,
    onSubmit: (serverUrl: String, username: String, password: String, passphrase: String, confirm: String) -> Unit,
    onRecoveryAcknowledged: () -> Unit,
) {
    var serverUrl by remember { mutableStateOf("") }
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var passphrase by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }

    val recoveryHex = (state as? UiState.Ready)?.data?.recoveryHex.orEmpty()
    if (recoveryHex.isNotEmpty()) {
        RecoveryKeySheet(recoveryHex = recoveryHex, onAcknowledged = onRecoveryAcknowledged)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(LocalWaystoneSpacing.current.Lg),
        verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md),
    ) {
        Text("Set up Waystone", style = LocalWaystoneTypography.current.xxxl)
        (state as? UiState.Error)?.let { ErrorBanner(it.message) }

        OutlinedTextField(
            value = serverUrl,
            onValueChange = { serverUrl = it },
            label = { Text("Server URL") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            enabled = state !is UiState.Loading,
        )
        OutlinedTextField(
            value = username,
            onValueChange = { username = it },
            label = { Text("Username (optional)") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            enabled = state !is UiState.Loading,
        )
        OutlinedTextField(
            value = password,
            onValueChange = { password = it },
            label = { Text("WebDAV password (optional)") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            modifier = Modifier.fillMaxWidth(),
            enabled = state !is UiState.Loading,
        )
        OutlinedTextField(
            value = passphrase,
            onValueChange = { passphrase = it },
            label = { Text("Vault passphrase") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            modifier = Modifier.fillMaxWidth(),
            enabled = state !is UiState.Loading,
        )
        OutlinedTextField(
            value = confirm,
            onValueChange = { confirm = it },
            label = { Text("Confirm passphrase") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            modifier = Modifier.fillMaxWidth(),
            enabled = state !is UiState.Loading,
        )

        Button(
            onClick = { onSubmit(serverUrl, username, password, passphrase, confirm) },
            enabled = state !is UiState.Loading,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(if (state is UiState.Loading) "Creating vault…" else "Create vault")
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecoveryKeySheet(recoveryHex: String, onAcknowledged: () -> Unit) {
    val clipboard = LocalClipboardManager.current
    ModalBottomSheet(onDismissRequest = onAcknowledged) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(LocalWaystoneSpacing.current.Lg),
            verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md),
        ) {
            Text("Save your recovery key", style = LocalWaystoneTypography.current.xxl)
            Text(
                "This is the only way to recover your vault if you forget the passphrase. Store it somewhere safe.",
                style = LocalWaystoneTypography.current.sm,
            )
            SelectionContainer {
                Text(recoveryHex, style = LocalWaystoneTypography.current.monoSm)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm)) {
                OutlinedButton(onClick = { clipboard.setText(AnnotatedString(recoveryHex)) }) {
                    Text("Copy")
                }
                Button(onClick = onAcknowledged) {
                    Text("I saved it")
                }
            }
        }
    }
}
