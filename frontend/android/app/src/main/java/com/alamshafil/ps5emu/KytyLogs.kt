// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

/**
 * Android-side access to the in-memory native log stream.
 */
object KytyLogs {
    private const val MAX_RECENT_LINES = 3000

    fun getRecentLogs(): skip.lib.Array<String> {
        val result = skip.lib.Array<String>()
        for (line in recentLogLines()) {
            result.append(line)
        }
        return result
    }

    fun recentLogLines(): List<String> {
        return nativeLogLines().takeLast(MAX_RECENT_LINES)
    }

    fun clear() {
        try {
            NativeBridge.nativeClearLogs()
        } catch (_: Throwable) {}
    }

    private fun nativeLogLines(): List<String> {
        return try {
            NativeBridge.nativeGetRecentLogs()?.toList().orEmpty()
        } catch (_: Throwable) {
            emptyList()
        }
    }
}
