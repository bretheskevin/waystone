package dev.waystone.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// Hardcoded port of design/tokens.json — mirrors desktop/src/tui/theme.rs (parity test guards it).
val Primary50 = Color(0xFFEEF2FF)
val Primary100 = Color(0xFFE0E7FF)
val Primary200 = Color(0xFFC7D2FE)
val Primary300 = Color(0xFFA5B4FC)
val Primary400 = Color(0xFF818CF8)
val Primary500 = Color(0xFF6366F1)
val Primary600 = Color(0xFF4F46E5)
val Primary700 = Color(0xFF4338CA)
val Primary800 = Color(0xFF3730A3)
val Primary900 = Color(0xFF312E81)

val Neutral50 = Color(0xFFFAFAFA)
val Neutral100 = Color(0xFFF4F4F5)
val Neutral200 = Color(0xFFE4E4E7)
val Neutral300 = Color(0xFFD4D4D8)
val Neutral400 = Color(0xFFA1A1AA)
val Neutral500 = Color(0xFF71717A)
val Neutral600 = Color(0xFF52525B)
val Neutral700 = Color(0xFF3F3F46)
val Neutral800 = Color(0xFF27272A)
val Neutral900 = Color(0xFF18181B)

val Success = Color(0xFF22C55E)
val Warning = Color(0xFFF59E0B)
val Error = Color(0xFFEF4444)
val Sync = Color(0xFF06B6D4)

val LightPrimary = Primary600
val DarkPrimary = Primary400

data class WaystoneColors(
    val primary: Color,
    val background: Color,
    val surface: Color,
    val surfaceVariant: Color,
    val textPrimary: Color,
    val textSecondary: Color,
    val border: Color,
    val success: Color = Success,
    val warning: Color = Warning,
    val error: Color = Error,
    val sync: Color = Sync,
)

fun lightWaystoneColors() = WaystoneColors(
    primary = LightPrimary,
    background = Neutral50,
    surface = Color.White,
    surfaceVariant = Neutral100,
    textPrimary = Neutral900,
    textSecondary = Neutral600,
    border = Neutral200,
)

fun darkWaystoneColors() = WaystoneColors(
    primary = DarkPrimary,
    background = Neutral900,
    surface = Neutral800,
    surfaceVariant = Neutral700,
    textPrimary = Neutral50,
    textSecondary = Neutral400,
    border = Neutral700,
)

// Accepts the wire status name, the camelCase alias, and data-layer SourceStatus names.
fun statusColor(status: String): Color = when (status) {
    "in_sync", "inSync", "InSync" -> Success
    "ahead", "Ahead", "behind", "Behind" -> Sync
    "conflict", "Conflict" -> Warning
    "error", "Error" -> Error
    else -> Neutral400
}

object WaystoneSpacing {
    val Xs: Dp = 4.dp
    val Sm: Dp = 8.dp
    val Md: Dp = 16.dp
    val Lg: Dp = 24.dp
    val Xl: Dp = 32.dp
    val Xxl: Dp = 48.dp
}

object WaystoneRadii {
    val Sm: Dp = 4.dp
    val Md: Dp = 8.dp
    val Lg: Dp = 12.dp
    val Full: Dp = 9999.dp
}

object WaystoneMotion {
    const val FastMs: Int = 100
    const val NormalMs: Int = 200
    const val SlowMs: Int = 350
}

// System fonts only — tokens name Inter/JetBrains Mono, no bundled downloads (YAGNI).
data class WaystoneTypography(
    val xs: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.Normal, fontSize = 12.sp),
    val sm: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.Normal, fontSize = 14.sp),
    val base: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.Normal, fontSize = 16.sp),
    val lg: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.Medium, fontSize = 18.sp),
    val xl: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.SemiBold, fontSize = 20.sp),
    val xxl: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.SemiBold, fontSize = 24.sp),
    val xxxl: TextStyle = TextStyle(fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.Bold, fontSize = 32.sp),
    val monoSm: TextStyle = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Normal, fontSize = 14.sp),
    val monoBase: TextStyle = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Normal, fontSize = 16.sp),
)

val LocalWaystoneColors = staticCompositionLocalOf { lightWaystoneColors() }
val LocalWaystoneSpacing = staticCompositionLocalOf { WaystoneSpacing }
val LocalWaystoneRadii = staticCompositionLocalOf { WaystoneRadii }
val LocalWaystoneMotion = staticCompositionLocalOf { WaystoneMotion }
val LocalWaystoneTypography = staticCompositionLocalOf { WaystoneTypography() }

@Composable
fun WaystoneTheme(useDark: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    val colors = if (useDark) darkWaystoneColors() else lightWaystoneColors()
    CompositionLocalProvider(
        LocalWaystoneColors provides colors,
        LocalWaystoneSpacing provides WaystoneSpacing,
        LocalWaystoneRadii provides WaystoneRadii,
        LocalWaystoneMotion provides WaystoneMotion,
        LocalWaystoneTypography provides WaystoneTypography(),
    ) {
        content()
    }
}
