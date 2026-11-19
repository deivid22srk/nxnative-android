package org.nx2a.launcher

import android.content.Context
import android.os.Build
import android.os.ParcelFileDescriptor
import android.os.StatFs
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class BuildFailure(val code: String, override val message: String) : Exception(message)

/**
 * On-device preparation pipeline. It performs real work on this phone:
 *
 *  1. re-validates the container and the key generation (native C++);
 *  2. proves the content keys unlock the ticket and the program NCA header
 *     (AES-128 ECB + XTS, native C++);
 *  3. extracts every NSP entry into private storage, resumable and with
 *     progress, driven chunk by chunk so cancellation is immediate;
 *  4. writes a manifest and a machine-readable report that can be exported.
 *
 * The shader decode / SPIR-V translation / runtime packaging stages are listed
 * as [StageState.SKIPPED] and are *not* claimed as done: they need the
 * nx2android translator and host, which this build does not embed.
 */
class PortPipeline(private val context: Context) {

    private val stageOrder = BuildStageId.entries
    private val weights = mapOf(
        BuildStageId.VERIFY to 0.05f,
        BuildStageId.KEYS to 0.10f,
        BuildStageId.EXTRACT to 0.80f,
        BuildStageId.REPORT to 0.05f,
        BuildStageId.RUNTIME to 0.0f,
    )

    private val stages = stageOrder.associateWith { BuildStageState(it) }.toMutableMap()
    private var totalBytes = 0L
    private var extractedBytes = 0L
    private var keysVerified = false
    private var headerMagic = ""
    private var contentType = -1
    private var reportPath = ""

    private lateinit var buildDir: File
    private lateinit var logFile: File

    suspend fun run(game: GameEntry, onUpdate: (BuildSnapshot) -> Unit): BuildSnapshot =
        withContext(Dispatchers.IO) {
            val startedAt = System.currentTimeMillis()
            buildDir = File(File(context.filesDir, "build"), game.titleId).apply { mkdirs() }
            logFile = File(buildDir, "build.log")
            log("build", "start ${game.titleId} (${game.name})")
            var snapshot = BuildSnapshot(
                titleId = game.titleId,
                gameName = game.name,
                running = true,
                startedAt = startedAt,
            )
            fun emit() {
                snapshot = snapshot.copy(
                    running = true,
                    finished = false,
                    stages = stageOrder.map { stages.getValue(it) },
                    overall = overall(),
                    extractedBytes = extractedBytes,
                    totalBytes = totalBytes,
                    keysVerified = keysVerified,
                    headerMagic = headerMagic,
                    contentType = contentType,
                    reportPath = reportPath,
                )
                onUpdate(snapshot)
            }

            try {
                val report = verify(game, ::emit)
                totalBytes = report.entries.sumOf { it.size }
                verifyKeys(game, ::emit)
                extract(report, ::emit)
                val finalSnapshot = finish(game, report, startedAt, snapshot, ::emit)
                log("build", "prepared ${finalSnapshot.extractedBytes} bytes")
                finalSnapshot
            } catch (cancel: CancellationException) {
                log("build", "cancelled")
                stages[BuildStageId.EXTRACT] = stages.getValue(BuildStageId.EXTRACT).copy(
                    state = StageState.FAILED,
                    detail = "cancelled",
                )
                val cancelled = snapshot.copy(
                    running = false,
                    finished = false,
                    cancelled = true,
                    finishedAt = System.currentTimeMillis(),
                    stages = stageOrder.map { stages.getValue(it) },
                    overall = overall(),
                    extractedBytes = extractedBytes,
                    totalBytes = totalBytes,
                )
                onUpdate(cancelled)
                throw cancel
            } catch (failure: BuildFailure) {
                log("build", "failed ${failure.code}: ${failure.message}")
                failSnapshot(snapshot, failure.code, failure.message, onUpdate)
            } catch (t: Throwable) {
                log("build", "failed ${t.javaClass.simpleName}: ${t.message.orEmpty()}")
                failSnapshot(
                    snapshot,
                    "ERR_BUILD",
                    t.message ?: context.getString(R.string.err_generic),
                    onUpdate,
                )
            }
        }

    private suspend fun verify(
        game: GameEntry,
        emit: () -> Unit,
    ): ImportReport {
        stages[BuildStageId.VERIFY] = BuildStageState(
            BuildStageId.VERIFY, StageState.ACTIVE, "reading the container", 0.1f,
        )
        emit()

        val nsp = ImportRepository(context).nspFile(game.titleId)
        if (!nsp.isFile || nsp.length() == 0L) {
            throw BuildFailure("ERR_IO", "the imported NSP is missing from private storage")
        }
        val prod = File(ImportRepository(context).keysDir(), "prod.keys")
        if (!prod.isFile) throw BuildFailure("ERR_KEYS", "prod.keys is not in private storage")

        val report = ParcelFileDescriptor.open(nsp, ParcelFileDescriptor.MODE_READ_ONLY).use { pfd ->
            Native.analyzePfd(pfd, prod.absolutePath)
        }
        log(
            "verify",
            "ok=${report.ok} title=${report.titleId} gen=${report.keyGeneration} " +
                "entries=${report.entries.size} keys=${report.keysCount} missing=${report.missingKeys.size}",
        )
        if (!report.ok) {
            throw BuildFailure(report.code, report.error)
        }
        if (!report.supported) {
            val names = report.missingKeys.joinToString(", ")
            throw BuildFailure(
                "ERR_KEYS",
                "prod.keys does not cover generation ${report.keyGeneration}" +
                    (if (names.isNotBlank()) ": missing $names" else ""),
            )
        }
        if (report.titleId != game.titleId) {
            throw BuildFailure("ERR_TITLE", "the container title id changed since import")
        }

        val free = try {
            StatFs(context.filesDir.absolutePath).availableBytes
        } catch (_: Throwable) {
            0L
        }
        val needed = (report.size * 1.02).toLong() + (256L shl 20)
        if (free in 1 until needed) {
            throw BuildFailure(
                "ERR_SPACE",
                "extraction needs about ${humanSize(needed)}, only ${humanSize(free)} free",
            )
        }

        stages[BuildStageId.VERIFY] = BuildStageState(
            BuildStageId.VERIFY, StageState.DONE,
            "${report.entries.size} entries, ${humanSize(report.size)}", 1f,
        )
        emit()
        return report
    }

    private suspend fun verifyKeys(game: GameEntry, emit: () -> Unit) {
        stages[BuildStageId.KEYS] = BuildStageState(
            BuildStageId.KEYS, StageState.ACTIVE, "decrypting the ticket title key", 0.2f,
        )
        emit()

        val nsp = ImportRepository(context).nspFile(game.titleId)
        val prod = File(ImportRepository(context).keysDir(), "prod.keys")
        val obj = try {
            JSONObject(Native.verifyContentKeys(nsp.absolutePath, prod.absolutePath))
        } catch (t: Throwable) {
            throw BuildFailure("ERR_KEYS", t.message ?: "native content check failed")
        }
        val titleKeyOk = obj.optBoolean("titleKeyOk")
        val headerOk = obj.optBoolean("headerOk")
        keysVerified = obj.optBoolean("ok")
        headerMagic = obj.optString("headerMagic")
        contentType = obj.optInt("contentType", -1)
        log(
            "keys",
            "titleKey=${obj.optString("titleKey")}=$titleKeyOk " +
                "header=${obj.optString("headerKey")}=$headerOk magic=$headerMagic type=$contentType",
        )
        if (!obj.optBoolean("ok")) {
            throw BuildFailure(
                obj.optString("code", "ERR_CONTENT"),
                obj.optString("error").ifBlank { "content keys did not verify" },
            )
        }
        stages[BuildStageId.KEYS] = BuildStageState(
            BuildStageId.KEYS, StageState.DONE,
            "ticket title key + NCA header verified ($headerMagic)", 1f,
        )
        emit()
    }

    private suspend fun extract(report: ImportReport, emit: () -> Unit) {
        stages[BuildStageId.EXTRACT] = BuildStageState(
            BuildStageId.EXTRACT, StageState.ACTIVE, "extracting entries", 0f,
        )
        emit()

        val nsp = ImportRepository(context).nspFile(report.titleId)
        val outDir = File(buildDir, "extracted").apply { mkdirs() }
        val manifest = StringBuilder("name\tkind\tsize\toffset\n")
        val used = mutableSetOf<String>()
        var files = 0

        ParcelFileDescriptor.open(nsp, ParcelFileDescriptor.MODE_READ_ONLY).use { pfd ->
            for ((index, entry) in report.entries.withIndex()) {
                currentCoroutineContext().ensureActive()
                if (entry.size <= 0) continue
                val safe = safeName(entry.name, index, used)
                val dest = File(outDir, safe)
                var done = if (dest.isFile && dest.length() in 1..entry.size) dest.length() else 0L
                if (dest.isFile && dest.length() > entry.size) {
                    dest.delete()
                    done = 0L
                }
                if (done > 0) extractedBytes += done  // resumed bytes count too
                while (done < entry.size) {
                    currentCoroutineContext().ensureActive()
                    val want = minOf(CHUNK, entry.size - done)
                    val copied = Native.copyRange(
                        pfd.fd,
                        entry.offset + done,
                        want,
                        dest.absolutePath,
                        done > 0,
                    )
                    if (copied != want) {
                        throw BuildFailure(
                            "ERR_IO",
                            "extraction of ${entry.name} stopped at $done of ${entry.size} bytes",
                        )
                    }
                    done += copied
                    extractedBytes += copied
                    val p = if (totalBytes > 0) {
                        (extractedBytes.toFloat() / totalBytes).coerceIn(0f, 1f)
                    } else 0f
                    stages[BuildStageId.EXTRACT] = BuildStageState(
                        BuildStageId.EXTRACT, StageState.ACTIVE,
                        "${index + 1}/${report.entries.size} · ${humanSize(extractedBytes)} / " +
                            humanSize(totalBytes),
                        p,
                    )
                    emit()
                }
                manifest.append(safe).append('\t').append(entry.kind).append('\t')
                    .append(entry.size).append('\t').append(entry.offset).append('\n')
                files++
            }
        }

        File(buildDir, "manifest.tsv").writeText(manifest.toString())
        stages[BuildStageId.EXTRACT] = BuildStageState(
            BuildStageId.EXTRACT, StageState.DONE,
            "$files files · ${humanSize(extractedBytes)}", 1f,
        )
        log("extract", "$files files, $extractedBytes bytes")
        emit()
    }

    private fun finish(
        game: GameEntry,
        report: ImportReport,
        startedAt: Long,
        base: BuildSnapshot,
        emit: () -> Unit,
    ): BuildSnapshot {
        stages[BuildStageId.REPORT] = BuildStageState(
            BuildStageId.REPORT, StageState.ACTIVE, "writing the report", 0.5f,
        )
        emit()

        val finishedAt = System.currentTimeMillis()
        val reportFile = File(buildDir, "build-report.json")
        val stagesArray = JSONArray()
        for (id in stageOrder) {
            val s = stages.getValue(id)
            stagesArray.put(
                JSONObject().apply {
                    put("id", id.name)
                    put("state", s.state.name)
                    put("detail", s.detail)
                },
            )
        }
        val obj = JSONObject().apply {
            put("schema", 1)
            put("titleId", game.titleId)
            put("game", game.name)
            put("generatedAt", isoNow())
            put("startedAt", startedAt)
            put("finishedAt", finishedAt)
            put("native", runCatching { Native.nativeVersion() }.getOrDefault("unknown"))
            put(
                "device",
                JSONObject().apply {
                    put("manufacturer", Build.MANUFACTURER)
                    put("model", Build.MODEL)
                    put("android", Build.VERSION.RELEASE)
                    put("api", Build.VERSION.SDK_INT)
                    put("abi", Build.SUPPORTED_ABIS.joinToString())
                },
            )
            put(
                "container",
                JSONObject().apply {
                    put("size", report.size)
                    put("entries", report.entries.size)
                },
            )
            put(
                "keys",
                JSONObject().apply {
                    put("count", report.keysCount)
                    put("maxGeneration", report.keysMaxGeneration)
                    put("requiredGeneration", report.keyGeneration)
                    put("titleKeyOk", keysVerified)
                    put("headerOk", headerMagic.isNotBlank())
                    put("headerMagic", headerMagic)
                    put("contentType", contentType)
                },
            )
            put(
                "pipeline",
                JSONObject().apply {
                    put("status", "PREPARED")
                    put("extractedBytes", extractedBytes)
                    put("files", report.entries.size)
                    put("runtimeIncluded", false)
                    put("shadersTranslated", false)
                    put("stages", stagesArray)
                },
            )
            put(
                "limitations",
                JSONArray().apply {
                    put("This build extracts and verifies the container; it does not embed the nx2android shader translator or host.")
                    put("The SKIPPED RUNTIME stage is not a success and is never reported as one.")
                },
            )
        }
        reportFile.writeText(obj.toString(2))
        reportPath = reportFile.absolutePath

        stages[BuildStageId.REPORT] = BuildStageState(
            BuildStageId.REPORT, StageState.DONE, "build-report.json", 1f,
        )
        stages[BuildStageId.RUNTIME] = BuildStageState(
            BuildStageId.RUNTIME, StageState.SKIPPED,
            "translator + host are not part of this build", 0f,
        )
        log("report", "wrote ${reportFile.name}")

        val finalSnapshot = base.copy(
            running = false,
            finished = true,
            failed = false,
            cancelled = false,
            stages = stageOrder.map { stages.getValue(it) },
            overall = 1f,
            extractedBytes = extractedBytes,
            totalBytes = totalBytes,
            keysVerified = keysVerified,
            headerMagic = headerMagic,
            contentType = contentType,
            reportPath = reportPath,
            startedAt = startedAt,
            finishedAt = finishedAt,
        )
        emit()
        return finalSnapshot
    }

    private fun failSnapshot(
        base: BuildSnapshot,
        code: String,
        message: String,
        onUpdate: (BuildSnapshot) -> Unit,
    ): BuildSnapshot {
        val active = stages.entries.firstOrNull { it.value.state == StageState.ACTIVE }?.key
            ?: BuildStageId.VERIFY
        stages[active] = stages.getValue(active).copy(state = StageState.FAILED, detail = message)
        val snap = base.copy(
            running = false,
            finished = false,
            failed = true,
            errorCode = code,
            error = message,
            stages = stageOrder.map { stages.getValue(it) },
            overall = overall(),
            extractedBytes = extractedBytes,
            totalBytes = totalBytes,
            keysVerified = keysVerified,
            finishedAt = System.currentTimeMillis(),
        )
        onUpdate(snap)
        return snap
    }

    private fun overall(): Float {
        var value = 0f
        for (id in stageOrder) {
            val s = stages.getValue(id)
            val weight = weights[id] ?: 0f
            value += when (s.state) {
                StageState.DONE -> weight
                StageState.SKIPPED -> 0f
                StageState.ACTIVE -> weight * s.progress.coerceIn(0f, 1f)
                else -> 0f
            }
        }
        return value.coerceIn(0f, 1f)
    }

    private fun safeName(raw: String, index: Int, used: MutableSet<String>): String {
        var name = raw.substringAfterLast('/').substringAfterLast('\\')
        name = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        if (name.isBlank() || name == "." || name == "..") name = "entry-$index"
        var candidate = name
        var n = 1
        while (!used.add(candidate)) {
            candidate = "$index-$n-$name"
            n++
        }
        return candidate
    }

    private fun log(tag: String, message: String) {
        val line = "${isoNow()} [$tag] $message"
        Logs.add(tag, message)
        runCatching { logFile.appendText(line + "\n") }
    }

    private fun isoNow(): String =
        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSZ", Locale.US).format(Date())

    companion object {
        private const val CHUNK = 16L shl 20
    }
}