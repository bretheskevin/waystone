package dev.waystone.data.session

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.io.File
import java.security.KeyStore
import java.util.logging.Level
import java.util.logging.Logger
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.spec.GCMParameterSpec

/**
 * AndroidKeyStore-backed [SessionStore]. AES-256/GCM key (alias "waystone_session",
 * no user-auth requirement so auto-unlock survives biometric changes); 12-byte IV is
 * prepended to each ciphertext blob under filesDir/session/.
 */
class KeystoreVaultStore(context: Context) : SessionStore {

    private val log = Logger.getLogger(KeystoreVaultStore::class.java.name)
    private val sessionDir = File(context.filesDir, "session")
    private val mdkFile get() = File(sessionDir, "mdk.bin")
    private val credsFile get() = File(sessionDir, "creds.bin")

    private fun secretKey(): java.security.Key {
        val ks = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (ks.getEntry(ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        kg.init(
            KeyGenParameterSpec.Builder(
                ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setUserAuthenticationRequired(false)
                .build(),
        )
        log.info("generated new session key (alias=$ALIAS)")
        return kg.generateKey()
    }

    private fun encrypt(plain: ByteArray): ByteArray {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, secretKey())
        val iv = cipher.iv
        require(iv.size == IV_LEN) { "unexpected GCM IV length ${iv.size}" }
        return iv + cipher.doFinal(plain)
    }

    private fun decrypt(blob: ByteArray): ByteArray {
        require(blob.size > IV_LEN) { "session blob too short (${blob.size})" }
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, secretKey(), GCMParameterSpec(TAG_BITS, blob, 0, IV_LEN))
        return cipher.doFinal(blob, IV_LEN, blob.size - IV_LEN)
    }

    override fun wrapMdk(mdk: ByteArray): Boolean {
        return try {
            sessionDir.mkdirs()
            mdkFile.writeBytes(encrypt(mdk))
            log.info("mdk wrapped -> ${mdkFile.path}")
            true
        } catch (e: Exception) {
            log.log(Level.WARNING, "wrapMdk failed", e)
            false
        } finally {
            mdk.fill(0)
        }
    }

    override fun unwrapMdk(): ByteArray? {
        if (!mdkFile.exists()) return null
        return try {
            decrypt(mdkFile.readBytes())
        } catch (e: Exception) {
            log.log(Level.WARNING, "unwrapMdk failed; caller falls back to passphrase", e)
            null
        }
    }

    override fun saveCreds(username: String, password: CharArray): Boolean {
        return try {
            val payload = (username + "\n" + String(password)).toByteArray()
            sessionDir.mkdirs()
            credsFile.writeBytes(encrypt(payload))
            log.info("creds saved -> ${credsFile.path}")
            true
        } catch (e: Exception) {
            log.log(Level.WARNING, "saveCreds failed", e)
            false
        } finally {
            password.fill('\u0000')
        }
    }

    override fun loadCreds(): Pair<String, CharArray>? {
        if (!credsFile.exists()) return null
        return try {
            val text = String(decrypt(credsFile.readBytes()))
            val idx = text.indexOf('\n')
            if (idx < 0) {
                log.warning("creds blob malformed")
                null
            } else {
                text.substring(0, idx) to text.substring(idx + 1).toCharArray()
            }
        } catch (e: Exception) {
            log.log(Level.WARNING, "loadCreds failed", e)
            null
        }
    }

    override fun clear() {
        for (f in listOf(mdkFile, credsFile)) {
            if (f.exists() && !f.delete()) log.warning("failed to delete ${f.path}")
        }
        log.info("session cleared")
    }

    companion object {
        private const val KEYSTORE = "AndroidKeyStore"
        private const val ALIAS = "waystone_session"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val IV_LEN = 12
        private const val TAG_BITS = 128
    }
}
