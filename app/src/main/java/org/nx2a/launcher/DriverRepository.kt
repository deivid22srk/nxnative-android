package org.nx2a.launcher

import android.content.Context
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipInputStream

/**
 * Turnip driver packs imported by the user as a .zip. Everything stays in the
 * app's private storage (libadrenotools refuses driver libraries from shared
 * storage). The pack is never bundled with the app: no driver is downloaded or
 * shipped here.
 */
data class DriverInfo(
    val id: String,
    val name: String,
    val soName: String,
    val directory: File,
    val importedAt: Long,
    val originalName: String,
)

class DriverRepository(private val context: Context) {

    fun root(): File = File(context.filesDir, "drivers").apply { mkdirs() }

    fun find(id: String): DriverInfo? {
        if (id.isBlank()) return null
        val dir = File(root(), id)
        val meta = File(dir, "driver.json")
        if (!meta.isFile) return null
        return try {
            val o = JSONObject(meta.readText())
            val so = o.optString("so")
            val soFile = File(dir, so)
            if (so.isBlank() || !soFile.isFile || soFile.length() == 0L) null
            else DriverInfo(
                id = id,
                name = o.optString("name", id),
                soName = so,
                directory = dir,
                importedAt = o.optLong("importedAt"),
                originalName = o.optString("original", ""),
            )
        } catch (_: Throwable) {
            null
        }
    }

    fun list(): List<DriverInfo> = root().listFiles()
        ?.filter { it.isDirectory }
        ?.mapNotNull { find(it.name) }
        ?.sortedByDescending { it.importedAt }
        ?: emptyList()

    /**
     * Extracts the smallest useful set from a Turnip zip: one driver .so plus
     * optional small metadata files. Rejects traversal, huge entries and packs
     * with no driver library.
     */
    suspend fun importZip(uri: Uri, originalName: String): DriverInfo = withContext(Dispatchers.IO) {
        val temp = File.createTempFile("driver", ".zip", context.cacheDir)
        try {
            context.contentResolver.openInputStream(uri).use { input ->
                requireNotNull(input) { "cannot open the selected zip" }
                temp.outputStream().use { output ->
                    val buffer = ByteArray(1 shl 20)
                    var total = 0L
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        total += n
                        if (total > MAX_ZIP_BYTES) {
                            throw IllegalArgumentException("driver zip is larger than ${MAX_ZIP_BYTES / (1 shl 20)} MB")
                        }
                        output.write(buffer, 0, n)
                    }
                }
            }

            var soEntry: String? = null
            var soName: String? = null
            var metaName: String? = null
            val extras = mutableListOf<Pair<String, ByteArray>>()

            ZipInputStream(temp.inputStream().buffered()).use { zip ->
                var entry = zip.nextEntry
                var count = 0
                var unpacked = 0L
                while (entry != null) {
                    if (++count > MAX_ENTRIES) throw IllegalArgumentException("driver zip has too many entries")
                    val rawName = entry.name.replace('\\', '/')
                    val base = rawName.substringAfterLast('/')
                    if (!entry.isDirectory && base.isNotBlank() && !rawName.startsWith("/") &&
                        !rawName.contains("..")
                    ) {
                        if (base.endsWith(".so", ignoreCase = true)) {
                            val bytes = readLimited(zip, MAX_SO_BYTES)
                            unpacked += bytes.size
                            if (soName == null || betterDriverName(base, soName!!)) {
                                soName = base
                                soEntry = rawName
                            }
                            extras += base to bytes
                        } else if (base.equals("meta.json", true)) {
                            metaName = base
                            extras += base to readLimited(zip, MAX_META_BYTES)
                        }
                    }
                    if (unpacked > MAX_UNPACKED_BYTES) {
                        throw IllegalArgumentException("driver zip expands to more than ${MAX_UNPACKED_BYTES / (1 shl 20)} MB")
                    }
                    zip.closeEntry()
                    entry = zip.nextEntry
                }
            }
            val driverSo = soName ?: throw IllegalArgumentException("no .so driver library found in the zip")
            val driverBytes = extras.firstOrNull { it.first == driverSo }?.second
                ?: throw IllegalArgumentException("driver library could not be read")

            val id = "drv-" + System.currentTimeMillis().toString(36) + "-" +
                driverSo.removeSuffix(".so").replace(Regex("[^A-Za-z0-9._-]"), "_").take(32)
            val dir = File(root(), id).apply { mkdirs() }
            FileOutputStream(File(dir, driverSo)).use { it.write(driverBytes) }

            var display = driverSo
            extras.firstOrNull { it.first == metaName }?.let { (_, bytes) ->
                File(dir, "meta.json").writeBytes(bytes)
                display = runCatching {
                    val meta = JSONObject(String(bytes))
                    meta.optString("name").ifBlank { meta.optString("driverName") }
                }.getOrNull().orEmpty().ifBlank { driverSo }
            }
            JSONObject().apply {
                put("name", display)
                put("so", driverSo)
                put("original", originalName)
                put("importedAt", System.currentTimeMillis())
                put("sourceEntry", soEntry ?: driverSo)
            }.toString().let { File(dir, "driver.json").writeText(it) }

            Logs.add("driver", "imported $driverSo (${driverBytes.size / (1 shl 20)} MB) as $id")
            find(id) ?: throw IllegalStateException("driver metadata was not written")
        } finally {
            temp.delete()
        }
    }

    fun delete(id: String) {
        find(id)?.directory?.deleteRecursively()
        Logs.add("driver", "deleted $id")
    }

    private fun readLimited(input: ZipInputStream, limit: Int): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(1 shl 16)
        var total = 0
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            total += n
            if (total > limit) throw IllegalArgumentException("driver entry is too large")
            out.write(buffer, 0, n)
        }
        return out.toByteArray()
    }

    private fun betterDriverName(candidate: String, current: String): Boolean {
        fun score(name: String): Int {
            val lower = name.lowercase()
            var s = 0
            if (lower.startsWith("vulkan")) s += 4
            if (lower.contains("freedreno")) s += 2
            if (lower.contains("turnip")) s += 2
            if (lower.contains("ad07") || lower.contains("ad08")) s += 1
            return s
        }
        return score(candidate) > score(current)
    }

    companion object {
        private const val MAX_ZIP_BYTES = 256L shl 20
        private const val MAX_UNPACKED_BYTES = 256L shl 20
        private const val MAX_SO_BYTES = 128 shl 20
        private const val MAX_META_BYTES = 1 shl 20
        private const val MAX_ENTRIES = 512
    }
}