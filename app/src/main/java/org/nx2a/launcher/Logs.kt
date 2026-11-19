package org.nx2a.launcher

import android.content.Context
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * In-app diagnostics ring buffer. Never log key material or file contents:
 * the import code only ever logs names, sizes, title ids and error codes.
 */
object Logs {
    private const val MAX_LINES = 2000
    private val lines = ArrayDeque<String>()

    fun add(tag: String, message: String) {
        val time = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
        synchronized(lines) {
            lines.addLast("$time [$tag] $message")
            while (lines.size > MAX_LINES) lines.removeFirst()
        }
    }

    fun snapshot(): List<String> = synchronized(lines) { lines.toList() }

    fun clear() = synchronized(lines) { lines.clear() }

    suspend fun export(context: Context, uri: Uri): Int = withContext(Dispatchers.IO) {
        val header = "nxnative-android diagnostics\n" +
            "device: ${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}\n" +
            "android: ${android.os.Build.VERSION.RELEASE} (API ${android.os.Build.VERSION.SDK_INT})\n" +
            "abi: ${android.os.Build.SUPPORTED_ABIS.joinToString()}\n" +
            "native: ${runCatching { Native.nativeVersion() }.getOrDefault("not loaded")}\n\n"
        val body = snapshot().joinToString("\n")
        context.contentResolver.openOutputStream(uri)?.use { out ->
            out.write((header + body + "\n").toByteArray())
        } ?: throw IllegalStateException("cannot open the export destination")
        snapshot().size
    }
}
