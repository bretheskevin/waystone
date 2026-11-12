package dev.waystone.app.vm

object SetupValidator {
    const val URL_ERROR = "Enter a valid http(s) server URL"
    const val PASSPHRASE_ERROR = "Passphrase must not be empty"
    const val CONFIRM_ERROR = "Passphrases do not match"

    fun isValidServerUrl(url: String): Boolean {
        val trimmed = url.trim()
        val withoutScheme = when {
            trimmed.startsWith("https://") -> trimmed.removePrefix("https://")
            trimmed.startsWith("http://") -> trimmed.removePrefix("http://")
            else -> return false
        }
        return withoutScheme.isNotBlank() && !withoutScheme.startsWith("/")
    }

    fun validateForm(serverUrl: String, encrypt: Boolean, passphrase: String, confirm: String): String? = when {
        !isValidServerUrl(serverUrl) -> URL_ERROR
        encrypt && passphrase.isEmpty() -> PASSPHRASE_ERROR
        encrypt && passphrase != confirm -> CONFIRM_ERROR
        else -> null
    }
}
