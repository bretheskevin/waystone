package dev.waystone.data.fs

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SafTreePathsTest {

    @Test
    fun `isSafeRelativePath accepts plain relative paths`() {
        assertTrue(SafTree.isSafeRelativePath("save.dat"))
        assertTrue(SafTree.isSafeRelativePath("slot0/data.sav"))
        assertTrue(SafTree.isSafeRelativePath("a/b/c/deep.bin"))
    }

    @Test
    fun `isSafeRelativePath trims leading slashes and backslashes`() {
        assertTrue(SafTree.isSafeRelativePath("/a/b.sav"))
        assertTrue(SafTree.isSafeRelativePath("\\a\\b.sav"))
        assertTrue(SafTree.isSafeRelativePath("//a//b.sav"))
        assertTrue(SafTree.isSafeRelativePath("\\\\a"))
    }

    @Test
    fun `isSafeRelativePath rejects dotdot segments anywhere`() {
        assertFalse(SafTree.isSafeRelativePath(".."))
        assertFalse(SafTree.isSafeRelativePath("../a.sav"))
        assertFalse(SafTree.isSafeRelativePath("a/../b.sav"))
        assertFalse(SafTree.isSafeRelativePath("a/b/../../c.sav"))
        assertFalse(SafTree.isSafeRelativePath("/../a.sav"))
    }

    @Test
    fun `isSafeRelativePath rejects empty paths`() {
        assertFalse(SafTree.isSafeRelativePath(""))
        assertFalse(SafTree.isSafeRelativePath("/"))
        assertFalse(SafTree.isSafeRelativePath("\\"))
    }

    @Test
    fun `normalizeRelativePath trims leading separators`() {
        assertEquals("a/b.sav", SafTree.normalizeRelativePath("/a/b.sav"))
        assertEquals("a\\b.sav", SafTree.normalizeRelativePath("\\a\\b.sav"))
        assertEquals("save.dat", SafTree.normalizeRelativePath("save.dat"))
    }

    @Test
    fun `normalizeRelativePath returns null for unsafe paths`() {
        assertNull(SafTree.normalizeRelativePath("../escape.sav"))
        assertNull(SafTree.normalizeRelativePath("a/../../b"))
        assertNull(SafTree.normalizeRelativePath(""))
        assertNull(SafTree.normalizeRelativePath("/"))
    }

    @Test
    fun `normalizeRelativePath keeps interior empty segments harmless`() {
        assertEquals("a//b.sav", SafTree.normalizeRelativePath("a//b.sav"))
    }
}
