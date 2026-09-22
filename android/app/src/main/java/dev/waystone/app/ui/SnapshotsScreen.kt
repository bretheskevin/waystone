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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.vm.SnapshotsReady
import dev.waystone.app.vm.UiState

@Composable
fun SnapshotsScreen(state: UiState<SnapshotsReady>, onRestore: (snapshotId: String) -> Unit) {
    when (state) {
        is UiState.Loading -> LoadingView()
        is UiState.Error -> ErrorView(state.message)
        is UiState.Ready -> LazyColumn(
            modifier = Modifier.fillMaxSize().padding(LocalWaystoneSpacing.current.Md),
            verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Sm),
        ) {
            items(state.data.snapshots, key = { it.id }) { snapshot ->
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.padding(LocalWaystoneSpacing.current.Md),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(snapshot.timestamp, style = LocalWaystoneTypography.current.base)
                            Text("Local safety snapshot", style = LocalWaystoneTypography.current.xs)
                        }
                        TextButton(onClick = { onRestore(snapshot.id) }) { Text("Restore") }
                    }
                }
            }
        }
    }
}
