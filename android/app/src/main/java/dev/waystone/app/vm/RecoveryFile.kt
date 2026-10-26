package dev.waystone.app.vm

object RecoveryFile {
    fun defaultName(deviceId: String) = "recovery-$deviceId.txt"
    fun content(hex: String) = hex + "\n"
}
