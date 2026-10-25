package dev.waystone.data.roms

import dev.waystone.data.config.ROM_KEYED_ADAPTER
import java.util.logging.Level
import java.util.logging.Logger
import uniffi.waystone_mobile.Confidence
import uniffi.waystone_mobile.NormalizedSave
import uniffi.waystone_mobile.Vault
import uniffi.waystone_mobile.WebDav
import uniffi.waystone_mobile.romKeyedSlots

/** Remote-only ROM slots (3DS semantics). Blocking WebDAV calls: invoke on an IO dispatcher. */
class RemoteOnlyProbe(private val vault: Vault, private val dav: WebDav) {

    private val log = Logger.getLogger("WS:SOURCES")

    fun find(roms: List<LocalRom>, localGroupKeys: Set<String>): List<NormalizedSave> {
        val out = mutableListOf<NormalizedSave>()
        for ((system, group) in roms.groupBy { it.system }) {
            val slots = try {
                romKeyedSlots(system)
            } catch (e: Exception) {
                log.log(Level.WARNING, "[roms] remote-only: no slot list for $system", e)
                continue
            }
            val pending = group.filter { rom -> slots.any { groupKey(rom, it) !in localGroupKeys } }
            if (pending.isEmpty()) continue
            val sysSeg = vault.pathSegment(system)
            val games = try {
                dav.propfind(sysSeg).map(::hrefLastSegment).toSet()
            } catch (e: Exception) {
                log.log(Level.WARNING, "[roms] remote-only: PROPFIND $system failed", e)
                continue
            }
            log.info("[roms] remote set $system: ${games.size} entr(y/ies), ${pending.size} ROM(s) to check")
            for (rom in pending) {
                val gameSeg = vault.pathSegment(rom.romId)
                if (gameSeg !in games) continue
                val remoteSlots = try {
                    dav.propfind("$sysSeg/$gameSeg").map(::hrefLastSegment).toSet()
                } catch (e: Exception) {
                    log.log(Level.WARNING, "[roms] remote-only: PROPFIND ${rom.system}/${rom.romId} failed", e)
                    continue
                }
                for (slot in slots) {
                    val gk = groupKey(rom, slot)
                    if (gk in localGroupKeys || vault.pathSegment(slot) !in remoteSlots) continue
                    log.info("[roms] remote-only $gk (${rom.romPath})")
                    out += remoteOnlySave(rom, slot)
                }
            }
        }
        log.info("[roms] remote-only probe done: ${out.size} slot(s)")
        return out
    }

    companion object {
        fun hrefLastSegment(href: String): String = href.trimEnd('/').substringAfterLast('/')

        private fun groupKey(rom: LocalRom, slot: String) = "${rom.system}/${rom.romId}/$slot"

        fun remoteOnlySave(rom: LocalRom, slot: String) = NormalizedSave(
            source = ROM_KEYED_ADAPTER, system = rom.system, gameKey = rom.romId, displayName = rom.displayName,
            titleId = null, serial = null, romCrc = null, confidence = Confidence.STRONG, slot = slot,
            kind = "battery", groupKey = groupKey(rom, slot), portable = true, mtime = "", files = emptyList(),
        )
    }
}
