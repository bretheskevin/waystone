package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.OutlinedTextField
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
import dev.waystone.app.vm.UiState
import dev.waystone.app.vm.UnlockReady

@Composable
fun UnlockScreen(
    state: UiState<UnlockReady>,
    passphrase: String,
    useRecoveryKey: Boolean,
    needsWebdavPassword: Boolean,
    onToggleRecovery: (Boolean) -> Unit,
    onSecretChange: (String) -> Unit,
    onWebdavPasswordChange: (String) -> Unit,
    onSubmit: () -> Unit,
) {
    var webdavPassword by remember { mutableStateOf("") }
    when (state) {
        is UiState.Loading -> LoadingView()
        else -> Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(LocalWaystoneSpacing.current.Lg),
            verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md),
        ) {
            Text("Unlock vault", style = LocalWaystoneTypography.current.xxxl)
            (state as? UiState.Error)?.let { ErrorBanner(it.message) }

            OutlinedTextField(
                value = passphrase,
                onValueChange = onSecretChange,
                label = { Text(if (useRecoveryKey) "Recovery key" else "Passphrase") },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                modifier = Modifier.fillMaxWidth(),
                enabled = state !is UiState.Loading,
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = useRecoveryKey, onCheckedChange = onToggleRecovery)
                Text("Use recovery key instead", style = LocalWaystoneTypography.current.sm)
            }
            if (needsWebdavPassword) {
                OutlinedTextField(
                    value = webdavPassword,
                    onValueChange = {
                        webdavPassword = it
                        onWebdavPasswordChange(it)
                    },
                    label = { Text("WebDAV password") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    modifier = Modifier.fillMaxWidth(),
                    enabled = state !is UiState.Loading,
                )
            }
            Button(
                onClick = onSubmit,
                enabled = state !is UiState.Loading,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Unlock")
            }
        }
    }
}
