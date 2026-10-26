package dev.waystone.app.log

import dev.waystone.data.log.WsLogFile

/** Drop-in for android.util.Log that also appends to the rotating log file. */
object Log {
    fun d(tag: String, msg: String, tr: Throwable? = null): Int = emit('D', tag, msg, tr, android.util.Log::d)

    fun i(tag: String, msg: String, tr: Throwable? = null): Int = emit('I', tag, msg, tr, android.util.Log::i)

    fun w(tag: String, msg: String, tr: Throwable? = null): Int = emit('W', tag, msg, tr, android.util.Log::w)

    fun e(tag: String, msg: String, tr: Throwable? = null): Int = emit('E', tag, msg, tr, android.util.Log::e)

    private inline fun emit(
        level: Char,
        tag: String,
        msg: String,
        tr: Throwable?,
        logcat: (String, String, Throwable?) -> Int,
    ): Int {
        WsLogFile.write(level, tag, msg, tr)
        return logcat(tag, msg, tr)
    }
}
