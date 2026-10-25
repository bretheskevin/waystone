package dev.waystone.data.config

/** Core SaveFlavor::Native: the rom_systems save naming (no external save tree). */
const val NATIVE_FLAVOR = "native"

/** Adapter id of ROM-keyed sources (every emulator preset uses it). */
const val ROM_KEYED_ADAPTER = "rom_keyed"

enum class EmulatorPreset(
    val id: String,
    val label: String,
    val flavor: String,
    val asksForSaves: Boolean,
    /** SAF document id for the saves picker's EXTRA_INITIAL_URI (primary storage). */
    val savesInitialDocId: String?,
) {
    RETROARCH("retroarch", "RetroArch", "retroarch", true, "primary:RetroArch/saves"),
    MELONDS("melonds", "melonDS", NATIVE_FLAVOR, false, null),
    DRASTIC("drastic", "DraStic", "drastic", true, "primary:DraStic/backup"),
    ;

    companion object {
        fun of(id: String?): EmulatorPreset? = entries.firstOrNull { it.id == id }
    }
}

/** Flavor of the external save tree, or null when the source has none. */
fun SourceFolder.externalFlavor(): String? =
    if (saveUri == null) null else EmulatorPreset.of(preset)?.flavor ?: NATIVE_FLAVOR
