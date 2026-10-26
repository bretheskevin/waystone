package dev.waystone.data.roms

import java.util.logging.Level
import java.util.logging.Logger
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.RomRef
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.remoteOnlySaves

/** Remote-only ROM slots (3DS semantics) via the shared Rust probe. Blocking: invoke on an IO dispatcher. */
class RemoteOnlyProbe(private val vault: Vault, private val dav: WebDav) {

    private val log = Logger.getLogger("WS:SOURCES")

    fun find(roms: List<LocalRom>, localGroupKeys: Set<String>): List<NormalizedSave> = try {
        remoteOnlySaves(vault, roms.map { RomRef(it.system, it.romId, it.displayName) }, localGroupKeys.toList(), dav)
            .also { log.info("[roms] remote-only probe done: ${it.size} slot(s) across ${roms.size} ROM(s)") }
    } catch (e: Exception) {
        log.log(Level.WARNING, "[roms] remote-only probe failed", e)
        emptyList()
    }
}
