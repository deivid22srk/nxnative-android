package org.nx2a.launcher

import android.content.Context
import android.net.Uri
import android.os.StatFs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

/** Private-storage side of the import: keys, copies, deletion. */
class ImportRepository(private val context: Context) {

    fun keysDir(): File = File(context.filesDir, "keys").apply { mkdirs() }

    fun gameDir(titleId: String): File = File(File(context.filesDir, "games"), titleId)

    fun nspFile(titleId: String): File = File(gameDir(titleId), "base.nsp")

    suspend fun copyKeys(uri: Uri, targetName: String): File = withContext(Dispatchers.IO) {
        val dest = File(keysDir(), targetName)
        context.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "cannot open $targetName" }
            dest.outputStream().use { output -> input.copyTo(output) }
        }
        dest.setReadable(false, false)
        dest.setReadable(true, true)
        dest.setWritable(false, false)
        dest.setWritable(true, true)
        dest
    }

    fun analyze(uri: Uri, keysPath: String): ImportReport = Native.analyze(context, uri, keysPath)

    fun availableBytes(): Long = try {
        StatFs(context.filesDir.absolutePath).availableBytes
    } catch (_: Throwable) {
        0L
    }

    /**
     * Streams the NSP once into private storage. Resumes from a `.part` file
     * after cancellation or process death: the SAF stream is skipped forward
     * to the byte count already on disk.
     */
    suspend fun copyNsp(
        uri: Uri,
        titleId: String,
        totalBytes: Long,
        onProgress: (copied: Long, total: Long) -> Unit,
    ): File = withContext(Dispatchers.IO) {
        val dir = gameDir(titleId).apply { mkdirs() }
        val dest = File(dir, "base.nsp")
        val part = File(dir, "base.nsp.part")

        if (dest.exists() && totalBytes > 0 && dest.length() == totalBytes) {
            onProgress(totalBytes, totalBytes)
            return@withContext dest
        }

        var copied = if (part.exists()) part.length() else 0L
        if (copied > totalBytes && totalBytes > 0) {
            part.delete()
            copied = 0L
        }

        context.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "cannot open the selected NSP" }
            var toSkip = copied
            val skipBuffer = ByteArray(1 shl 16)
            while (toSkip > 0) {
                val n = input.read(skipBuffer, 0, minOf(skipBuffer.size.toLong(), toSkip).toInt())
                if (n < 0) break
                toSkip -= n
                if (!currentCoroutineContext().isActive) return@withContext part
            }
            FileOutputStream(part, true).use { output ->
                val buffer = ByteArray(1 shl 20)
                var lastReport = 0L
                while (true) {
                    if (!currentCoroutineContext().isActive) return@withContext part
                    val n = input.read(buffer)
                    if (n < 0) break
                    output.write(buffer, 0, n)
                    copied += n
                    if (copied - lastReport >= (8L shl 20)) {
                        lastReport = copied
                        onProgress(copied, if (totalBytes > 0) totalBytes else copied)
                    }
                }
                output.fd.sync()
            }
        }
        if (totalBytes > 0 && part.length() != totalBytes) {
            throw IllegalStateException("copy incomplete: ${part.length()} of $totalBytes bytes")
        }
        if (!part.renameTo(dest)) throw IllegalStateException("cannot finalise the NSP copy")
        onProgress(totalBytes, totalBytes)
        dest
    }

    fun deleteGame(titleId: String) {
        gameDir(titleId).deleteRecursively()
    }

    fun displayName(uri: Uri): String {
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) {
                val name = cursor.getString(index)
                if (!name.isNullOrBlank()) return name
            }
        }
        return uri.lastPathSegment ?: "arquivo"
    }
}

/** "Hogwarts Legacy [0100F7E00C70E000][v0].nsp" -> "Hogwarts Legacy" */
fun friendlyGameName(fileName: String): String {
    var name = fileName.substringBeforeLast('.')
    name = name.replace(Regex("\\[[^\\]]*\\]"), " ")
    name = name.replace(Regex("\\([^)]*\\)"), " ")
    name = name.substringBefore(" - ").trim()
    name = name.replace(Regex("\\s+"), " ")
    return name.ifBlank { "Jogo importado" }
}
