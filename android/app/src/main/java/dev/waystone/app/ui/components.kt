package dev.waystone.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import dev.waystone.app.ui.theme.LocalWaystoneColors
import dev.waystone.app.ui.theme.LocalWaystoneRadii
import dev.waystone.app.ui.theme.LocalWaystoneSpacing
import dev.waystone.app.ui.theme.LocalWaystoneTypography
import dev.waystone.app.ui.theme.statusColor

@Composable
fun LoadingView(modifier: Modifier = Modifier) {
    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        CircularProgressIndicator(color = LocalWaystoneColors.current.primary)
    }
}

@Composable
fun ErrorView(message: String, modifier: Modifier = Modifier) {
    Box(modifier = modifier.fillMaxSize().padding(LocalWaystoneSpacing.current.Lg), contentAlignment = Alignment.Center) {
        Text(message, color = LocalWaystoneColors.current.error, style = LocalWaystoneTypography.current.base)
    }
}

@Composable
fun StatusBadge(status: String, modifier: Modifier = Modifier) {
    val color = statusColor(status)
    Text(
        text = status,
        style = LocalWaystoneTypography.current.xs,
        color = LocalWaystoneColors.current.background,
        modifier = modifier
            .background(color, RoundedCornerShape(LocalWaystoneRadii.current.Full))
            .padding(horizontal = LocalWaystoneSpacing.current.Sm, vertical = LocalWaystoneSpacing.current.Xs),
    )
}

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
    Text(text, style = LocalWaystoneTypography.current.xl, color = MaterialTheme.colorScheme.onBackground, modifier = modifier)
}

@Composable
fun ErrorBanner(message: String, modifier: Modifier = Modifier) {
    Text(
        text = message,
        style = LocalWaystoneTypography.current.sm,
        color = LocalWaystoneColors.current.error,
        modifier = modifier
            .fillMaxWidth()
            .background(LocalWaystoneColors.current.error.copy(alpha = 0.12f))
            .padding(LocalWaystoneSpacing.current.Sm),
    )
}
