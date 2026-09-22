package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.HistoryReady
import dev.waystone.app.vm.UiState

@Composable
fun HistoryScreen(state: UiState<HistoryReady>, onRestore: (hash: String) -> Unit) {
    when (state) {
        is UiState.Loading -> LoadingView()
        is UiState.Error -> ErrorView(state.message)
        is UiState.Ready -> LazyColumn(
            modifier = Modifier.fillMaxSize().padding(LocalWaystoneSpacing.current.Md),
            verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm),
        ) {
            items(state.data.entries, key = { it.hash }) { entry ->
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.padding(LocalWaystoneSpacing.current.Md),
                        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(entry.timestamp, style = LocalWaystoneTypography.current.base)
                            Text(
                                "device ${entry.deviceId} · mtime ${entry.mtime}",
                                style = LocalWaystoneTypography.current.xs,
                            )
                            Text(entry.hash.take(12), style = LocalWaystoneTypography.current.monoSm)
                        }
                        TextButton(onClick = { onRestore(entry.hash) }) { Text("Restore") }
                    }
                }
            }
        }
    }
}
