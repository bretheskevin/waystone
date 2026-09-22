package dev.waystone.data.sync

import java.nio.charset.StandardCharsets
import java.util.logging.Logger
import uniffi.waystone_mobile.FileEntry

/**
 * Port of core/src/packaging.rs::unzip — STORED-only canonical zip reader.
 * Walks local file headers sequentially; stops at the first non-local-header
 * signature (central directory / EOCD). Hand-rolled, no zip library.
 */
object CanonicalUnzip {

    private val log = Logger.getLogger(CanonicalUnzip::class.java.name)

    class InvalidZipException(message: String) : Exception("invalid zip: $message")

    private const val LOCAL_HEADER_SIG = 0x04034b50L

    fun unzip(zip: ByteArray): List<FileEntry> {
        val entries = mutableListOf<FileEntry>()
        var pos = 0
        val len = zip.size

        while (pos + 4 <= len) {
            val sig = readU32(zip, pos)
            if (sig != LOCAL_HEADER_SIG) break
            if (pos + 30 > len) throw InvalidZipException("truncated local header")
            val method = readU16(zip, pos + 8)
            if (method != 0) throw InvalidZipException("only STORE (method 0) supported")
            val compSize = readU32(zip, pos + 18).toInt()
            val nameLen = readU16(zip, pos + 26).toInt()
            val extraLen = readU16(zip, pos + 28).toInt()

            val nameStart = pos + 30
            val nameEnd = nameStart + nameLen
            if (nameEnd > len) throw InvalidZipException("truncated file name")
            val name = try {
                String(zip, nameStart, nameLen, StandardCharsets.UTF_8)
            } catch (e: Exception) {
                throw InvalidZipException("invalid utf-8 file name")
            }

            val dataStart = Math.addExact(nameEnd, extraLen)
            val dataEnd = Math.addExact(dataStart, compSize)
            if (dataEnd > len) throw InvalidZipException("truncated file data")

            entries.add(FileEntry(name, zip.copyOfRange(dataStart, dataEnd)))
            log.fine("unzipped member $name (${compSize}B)")
            pos = dataEnd
        }

        entries.sortBy { it.path }
        log.info("unzipped ${entries.size} members from ${len}B archive")
        return entries
    }

    private fun readU16(buf: ByteArray, offset: Int): Int {
        if (offset + 2 > buf.size) throw InvalidZipException("truncated field")
        return (buf[offset].toInt() and 0xFF) or
            ((buf[offset + 1].toInt() and 0xFF) shl 8)
    }

    private fun readU32(buf: ByteArray, offset: Int): Long {
        if (offset + 4 > buf.size) throw InvalidZipException("truncated field")
        return (buf[offset].toLong() and 0xFF) or
            ((buf[offset + 1].toLong() and 0xFF) shl 8) or
            ((buf[offset + 2].toLong() and 0xFF) shl 16) or
            ((buf[offset + 3].toLong() and 0xFF) shl 24)
    }
}
