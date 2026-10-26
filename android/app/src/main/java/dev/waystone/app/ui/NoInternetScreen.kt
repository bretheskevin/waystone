package dev.waystone.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.data.net.NetStatusText
import dev.waystone.data.net.NoInternetReason

/** [onRetry] returns true when the connection is back (the caller navigates away). */
@Composable
fun NoInternetScreen(reason: NoInternetReason, onRetry: () -> Boolean, onExit: () -> Unit) {
    var retriedOffline by rememberSaveable { mutableStateOf(false) }
    Column(
        modifier = Modifier.fillMaxSize().padding(LocalWaystoneSpacing.current.Lg),
        verticalArrangement = Arrangement.spacedBy(LocalWaystoneSpacing.current.Md, Alignment.CenterVertically),
    ) {
        Text(NetStatusText.headline(reason), style = LocalWaystoneTypography.current.xxl)
        Text(NetStatusText.detail(reason), style = LocalWaystoneTypography.current.base)
        if (retriedOffline) Text(NetStatusText.RETRY_SUFFIX, style = LocalWaystoneTypography.current.sm)
        Button(onClick = { retriedOffline = !onRetry() }, modifier = Modifier.fillMaxWidth()) { Text("Retry") }
        OutlinedButton(onClick = onExit, modifier = Modifier.fillMaxWidth()) { Text("Exit") }
    }
}
