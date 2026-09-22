package dev.waystone.data.sync

import java.security.MessageDigest
import java.util.Base64
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class CanonicalUnzipTest {

    private fun sha256(data: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }

    // Golden vector from core/tests/golden_vectors.rs::golden_single_file_zip_and_hash:
    // canonical_zip([("save.dat", "Hello")]) -> 119 bytes, sha256 66dc6c12...
    private val goldenSingle = Base64.getDecoder().decode(
        "UEsDBBQAAAAAAAAAIQCCidH3BQAAAAUAAAAIAAAAc2F2ZS5kYXRIZWxsb1BLAQIUABQAAAAAAAAAIQCCidH3BQAAAAUAAAAIAAAAAAAAAAAAAAAAAAAAAABzYXZlLmRhdFBLBQYAAAAAAQABADYAAAArAAAAAAA="
    )

    private val goldenMulti = Base64.getDecoder().decode(
        "UEsDBBQAAAAAAAAAIQAzmjBxDQAAAA0AAAAJAAAAbWV0YS5qc29ueyJ2ZXJzaW9uIjoxfVBLAwQUAAAAAAAAACEAWqOcfAQAAAAEAAAADgAAAHNhdmVzL21haW4uc2F23q2+71BLAQIUABQAAAAAAAAAIQAzmjBxDQAAAA0AAAAJAAAAAAAAAAAAAAAAAAAAAABtZXRhLmpzb25QSwECFAAUAAAAAAAAACEAWqOcfAQAAAAEAAAADgAAAAAAAAAAAAAAAAA0AAAAc2F2ZXMvbWFpbi5zYXZQSwUGAAAAAAIAAgBzAAAAZAAAAAAA"
    )

    @Test
    fun `golden vector bytes match expected length and hash`() {
        assertEquals(119, goldenSingle.size)
        assertEquals(
            "66dc6c1281582edd83ee325e37f45d235c7e60d859f8117dd5d3a978117ee318",
            sha256(goldenSingle)
        )
    }

    @Test
    fun `golden single-file zip unzips to original member`() {
        val entries = CanonicalUnzip.unzip(goldenSingle)
        assertEquals(1, entries.size)
        assertEquals("save.dat", entries[0].path)
        assertArrayEquals("Hello".toByteArray(), entries[0].content)
    }

    @Test
    fun `golden multi-file zip unzips sorted with exact contents`() {
        val entries = CanonicalUnzip.unzip(goldenMulti)
        assertEquals(listOf("meta.json", "saves/main.sav"), entries.map { it.path })
        assertArrayEquals("{\"version\":1}".toByteArray(), entries[0].content)
        assertArrayEquals(byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0xBE.toByte(), 0xEF.toByte()), entries[1].content)
    }

    @Test
    fun `empty zip yields no entries`() {
        // EOCD only: sig + 18 zero bytes.
        val empty = byteArrayOf(0x50, 0x4B, 0x05, 0x06) + ByteArray(18)
        assertTrue(CanonicalUnzip.unzip(empty).isEmpty())
    }

    @Test
    fun `deflate method is rejected`() {
        val zip = goldenSingle.copyOf()
        zip[8] = 8 // method offset in local header
        assertInvalid(zip)
    }

    @Test
    fun `truncated zip is rejected`() {
        assertInvalid(goldenSingle.copyOfRange(0, 40))  // cut inside file data
        assertInvalid(goldenSingle.copyOfRange(0, 10))  // cut inside local header
    }

    @Test
    fun `tail shorter than a header signature yields no entries`() {
        assertTrue(CanonicalUnzip.unzip(ByteArray(2)).isEmpty())
    }

    private fun assertInvalid(zip: ByteArray) {
        try {
            CanonicalUnzip.unzip(zip)
            fail("expected InvalidZipException")
        } catch (e: CanonicalUnzip.InvalidZipException) {
            // expected
        }
    }
}
