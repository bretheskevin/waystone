package dev.waystone.data.sync

import kotlinx.coroutines.CancellationException

/** [runCatching] for coroutine code: cancellation is rethrown instead of being captured as a failure. */
inline fun <T> runCatchingCancellable(block: () -> T): Result<T> =
    try {
        Result.success(block())
    } catch (e: CancellationException) {
        throw e
    } catch (e: Exception) {
        Result.failure(e)
    }
