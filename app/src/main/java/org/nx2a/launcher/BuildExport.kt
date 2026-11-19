package org.nx2a.launcher

import android.content.Context
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.BufferedOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Small diagnostic bundle: report, manifest and logs. The extracted game data
 * (many GB) is intentionally not zipped; it stays in private storage and the
 * report says so.
 */
object BuildExport {

    data class Result(val files: Int, val bytes: Long)

    suspend fun export(context: Context, uri: Uri, titleId: String): Result =
        withContext(Dispatchers.IO) {
            val dir = File(File(context.filesDir, "build"), titleId)
            require(dir.isDirectory) { "no build directory for $titleId" }
            val wanted = listOf("build-report.json", "manifest.tsv", "build.log")
            var files = 0
            var bytes = 0L
            context.contentResolver.openOutputStream(uri)?.use { stream ->
                ZipOutputStream(BufferedOutputStream(stream)).use { zip ->
                    for (name in wanted) {
                        val file = File(dir, name)
                        if (!file.isFile) continue
                        zip.putNextEntry(ZipEntry(name))
                        file.inputStream().use { input ->
                            val buffer = ByteArray(1 shl 16)
                            while (true) {
                                val n = input.read(buffer)
                                if (n < 0) break
                                zip.write(buffer, 0, n)
                                bytes += n
                            }
                        }
                        zip.closeEntry()
                        files++
                    }
                    val device = buildString {
                        append("nxnative-android build bundle\n")
                        append("title id: $titleId\n")
                        append("device: ${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}\n")
                        append("android: ${android.os.Build.VERSION.RELEASE} (API ${android.os.Build.VERSION.SDK_INT})\n")
                        append("abi: ${android.os.Build.SUPPORTED_ABIS.joinToString()}\n")
                        append("native: ${runCatching { Native.nativeVersion() }.getOrDefault("unknown")}\n")
                        append("extracted data stays in the app's private storage.\n\n")
                        append(Logs.snapshot().joinToString("\n"))
                        append("\n")
                    }
                    zip.putNextEntry(ZipEntry("diagnostics.txt"))
                    zip.write(device.toByteArray())
                    zip.closeEntry()
                    files++
                    bytes += device.toByteArray().size
                }
            } ?: throw IllegalStateException("cannot open the export destination")
            Result(files, bytes)
        }
}