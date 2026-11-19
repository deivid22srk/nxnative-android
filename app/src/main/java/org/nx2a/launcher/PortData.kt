package org.nx2a.launcher

import android.content.Context
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.zip.ZipInputStream

/** The port's data pack, kept outside the app so a browser can fill it. */
object PortData {
    val root: File get() = File("/sdcard/HogwartsLegacy-data")

    fun ready(): Boolean =
        File(root, "RomFS").isDirectory &&
            File(root, "shaders.dmsv").isFile &&
            File(root, "mods").isDirectory

    fun logFile(): File? = File(root, "log.txt").takeIf { it.isFile }

    fun previousLogFile(): File? = File(root, "log.prev.txt").takeIf { it.isFile }

    /**
     * Extracts a data-pack .zip. The archive has a top-level
     * HogwartsLegacy-data/ directory; it is extracted below /sdcard exactly
     * there. Entries outside it are rejected (zip-slip).
     */
    suspend fun importZip(
        context: Context,
        uri: Uri,
        onProgress: (copied: Long, entries: Int) -> Unit,
    ): Long = withContext(Dispatchers.IO) {
        val target = root
        target.mkdirs()
        var copied = 0L
        var count = 0
        var total = 0L
        context.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "cannot open the selected zip" }
            ZipInputStream(input.buffered()).use { zip ->
                var entry = zip.nextEntry
                while (entry != null) {
                    val name = entry.name.replace('\\', '/')
                    if (!entry.isDirectory && name.isNotBlank()) {
                        if (!name.startsWith("HogwartsLegacy-data/")) {
                            throw IllegalArgumentException("unexpected entry in the pack: $name")
                        }
                        val out = File("/sdcard", name)
                        val canonical = out.canonicalFile
                        if (!canonical.path.startsWith(target.canonicalFile.path + File.separator)) {
                            throw IllegalArgumentException("entry escapes the data folder: $name")
                        }
                        canonical.parentFile?.mkdirs()
                        canonical.outputStream().use { output ->
                            val buffer = ByteArray(1 shl 20)
                            while (true) {
                                val n = zip.read(buffer)
                                if (n < 0) break
                                output.write(buffer, 0, n)
                                copied += n
                                total += n
                                onProgress(total, count)
                            }
                        }
                        count++
                    }
                    zip.closeEntry()
                    entry = zip.nextEntry
                }
            }
        }
        Logs.add("port", "data pack imported: $count files, ${humanSize(copied)}")
        copied
    }
}