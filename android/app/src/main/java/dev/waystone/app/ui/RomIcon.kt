package dev.waystone.app.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import dev.waystone.app.ui.theme.Primary600
import dev.waystone.app.ui.theme.Success
import dev.waystone.app.ui.theme.Sync
import dev.waystone.app.ui.theme.Warning
import dev.waystone.app.ui.theme.Primary800
import dev.waystone.app.ui.theme.Neutral600
import dev.waystone.app.vm.SaveRow
import dev.waystone.data.roms.RomArt
import java.nio.ByteBuffer
import uniffi.waystone_mobile.romSystemBadge

private val Tints = listOf(Primary600, Sync, Success, Warning, Primary800, Neutral600)
private const val ICON_DECODE_PX = 160

/** Stable per-system tint: same system always gets the same colour. */
internal fun systemTint(system: String): Color = Tints[(system.hashCode() and Int.MAX_VALUE) % Tints.size]

fun RomArt.toImageBitmap(): ImageBitmap? = when (this) {
    is RomArt.Rgba -> Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        .apply { copyPixelsFromBuffer(ByteBuffer.wrap(pixels)) }
        .asImageBitmap()
    is RomArt.Encoded -> decodeSampled(bytes, ICON_DECODE_PX)?.asImageBitmap()
}

/** Box art may be a multi-megapixel PNG; subsample so a 40dp tile never holds a full-size bitmap. */
private fun decodeSampled(bytes: ByteArray, maxSide: Int): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
    var sample = 1
    while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= maxSide) sample *= 2
    return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
}

/** ROM rows only; [load] is main-safe and the caller caches the decoded bitmap. */
@Composable
fun RomIcon(row: SaveRow, load: suspend (String) -> ImageBitmap?) {
    val system = row.system ?: return
    val art by produceState<ImageBitmap?>(null, row.artKey) {
        value = row.artKey?.let { load(it) }
    }
    val shape = RoundedCornerShape(8.dp)
    val bitmap = art
    if (bitmap != null) {
        Image(
            bitmap = bitmap,
            contentDescription = row.displayName,
            contentScale = ContentScale.Fit,
            filterQuality = FilterQuality.None,
            modifier = Modifier.size(40.dp).clip(shape),
        )
    } else {
        Box(
            modifier = Modifier.size(40.dp).clip(shape).background(systemTint(system)),
            contentAlignment = Alignment.Center,
        ) {
            Text(romSystemBadge(system), color = Color.White)
        }
    }
}
