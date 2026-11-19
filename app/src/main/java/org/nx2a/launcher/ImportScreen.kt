package org.nx2a.launcher

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import org.json.JSONObject

private data class Picked(val uri: Uri, val name: String)

@Composable
private fun PickerRow(
    label: String,
    picked: Picked?,
    enabled: Boolean,
    onPick: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text(
                text = picked?.name ?: stringResource(R.string.not_selected),
                style = MaterialTheme.typography.bodySmall,
                fontFamily = if (picked != null) FontFamily.Monospace else FontFamily.Default,
            )
        }
        OutlinedButton(onClick = onPick, enabled = enabled) {
            Text(stringResource(R.string.choose))
        }
    }
}

@Composable
fun ImportScreen(
    repository: AppRepository,
    importer: ImportRepository,
    onDone: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var nsp by remember { mutableStateOf<Picked?>(null) }
    var prodKeys by remember { mutableStateOf<Picked?>(null) }
    var titleKeys by remember { mutableStateOf<Picked?>(null) }
    var busy by remember { mutableStateOf(false) }
    var progress by remember { mutableFloatStateOf(0f) }
    var phase by remember { mutableStateOf("") }
    var error by remember { mutableStateOf<String?>(null) }
    var report by remember { mutableStateOf<ImportReport?>(null) }
    var imported by remember { mutableStateOf<GameEntry?>(null) }
    var job by remember { mutableStateOf<Job?>(null) }

    fun pick(set: (Picked) -> Unit) = { uri: Uri? ->
        if (uri != null) {
            try {
                context.contentResolver.takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION,
                )
            } catch (_: SecurityException) {
                // Some providers do not offer persistable grants; the copy
                // happens in this session, which is enough.
            }
            set(Picked(uri, importer.displayName(uri)))
        }
    }

    val pickNsp = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri -> pick { nsp = it }(uri) }
    val pickProd = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri -> pick { prodKeys = it }(uri) }
    val pickTitle = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri -> pick { titleKeys = it }(uri) }

    fun describe(code: String, detail: String): String = when (code) {
        "ERR_XCI" -> context.getString(R.string.err_xci)
        "ERR_MAGIC" -> context.getString(R.string.err_magic)
        "ERR_TICKET" -> context.getString(R.string.err_ticket)
        "ERR_TRUNCATED" -> context.getString(R.string.err_truncated)
        "ERR_ENTRIES" -> context.getString(R.string.err_entries)
        "ERR_IO" -> context.getString(R.string.err_io)
        else -> detail.ifBlank { context.getString(R.string.err_generic) }
    }

    fun startImport() {
        val nspUri = nsp?.uri
        val keysUri = prodKeys?.uri
        if (nspUri == null || keysUri == null || busy) return
        if (!(nsp?.name?.lowercase()?.endsWith(".nsp") == true)) {
            error = context.getString(R.string.err_not_nsp)
            return
        }
        error = null
        report = null
        imported = null
        progress = 0f
        busy = true
        job = scope.launch {
            try {
                phase = context.getString(R.string.phase_keys)
                val keysFile = importer.copyKeys(keysUri, "prod.keys")
                val titleUri = titleKeys?.uri
                if (titleUri != null) importer.copyKeys(titleUri, "title.keys")

                val keysJson = JSONObject(Native.loadKeys(keysFile.absolutePath))
                if (!keysJson.optBoolean("ok")) {
                    error = keysJson.optString("error").ifBlank {
                        context.getString(R.string.err_keys_format)
                    }
                    return@launch
                }

                phase = context.getString(R.string.phase_analyze)
                val result = importer.analyze(nspUri, keysFile.absolutePath)
                report = result
                if (!result.ok) {
                    error = describe(result.code, result.error)
                    Logs.add("import", "inspect failed ${result.code}")
                    return@launch
                }
                Logs.add(
                    "import",
                    "NSP ${result.titleId} keygen=${result.keyGeneration} " +
                        "keys=${result.keysCount} missing=${result.missingKeys.size}",
                )
                if (!result.supported) {
                    // Rule: tell the user and stop. Nothing is copied.
                    return@launch
                }

                val free = importer.availableBytes()
                val needed = (result.size * 1.1).toLong()
                if (free in 1 until needed) {
                    error = context.getString(
                        R.string.err_space,
                        humanSize(needed),
                        humanSize(free),
                    )
                    return@launch
                }

                phase = context.getString(R.string.phase_copy)
                val dest = importer.copyNsp(
                    nspUri,
                    result.titleId,
                    result.size,
                ) { copied, total ->
                    progress = if (total > 0) (copied.toFloat() / total).coerceIn(0f, 1f) else 0f
                }
                val entry = GameEntry(
                    titleId = result.titleId,
                    name = friendlyGameName(nsp?.name ?: "Jogo importado"),
                    sizeBytes = dest.length(),
                    keyGeneration = result.keyGeneration,
                    importedAt = System.currentTimeMillis(),
                    status = GameEntry.STATUS_VALIDATED,
                    nspFile = dest.relativeTo(context.filesDir).path,
                )
                repository.upsertGame(entry)
                imported = entry
                Logs.add("import", "stored ${result.titleId} (${humanSize(dest.length())})")
            } catch (cancel: CancellationException) {
                Logs.add("import", "cancelled")
                throw cancel
            } catch (t: Throwable) {
                error = t.message ?: context.getString(R.string.err_generic)
                Logs.add("import", "failed: ${t.javaClass.simpleName}")
            } finally {
                busy = false
                phase = ""
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        Text(
            text = stringResource(R.string.import_title),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.SemiBold,
        )
        Text(
            text = stringResource(R.string.import_body),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(top = 6.dp),
        )
        Text(
            text = stringResource(R.string.legal_note),
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(top = 6.dp),
        )
        Spacer(Modifier.height(16.dp))

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                PickerRow(
                    label = stringResource(R.string.pick_rom),
                    picked = nsp,
                    enabled = !busy,
                    onPick = { pickNsp.launch(arrayOf("*/*")) },
                )
                PickerRow(
                    label = stringResource(R.string.pick_prod),
                    picked = prodKeys,
                    enabled = !busy,
                    onPick = { pickProd.launch(arrayOf("*/*")) },
                )
                PickerRow(
                    label = stringResource(R.string.pick_title_keys),
                    picked = titleKeys,
                    enabled = !busy,
                    onPick = { pickTitle.launch(arrayOf("*/*")) },
                )
            }
        }

        Spacer(Modifier.height(14.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Button(
                onClick = { startImport() },
                enabled = !busy && nsp != null && prodKeys != null,
            ) { Text(stringResource(R.string.import_action)) }
            if (busy) {
                TextButton(onClick = { job?.cancel() }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        }

        if (busy) {
            Spacer(Modifier.height(14.dp))
            Text(phase, style = MaterialTheme.typography.bodySmall)
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 6.dp),
            )
            Text(
                text = "${(progress * 100).toInt()}%",
                style = MaterialTheme.typography.labelSmall,
                modifier = Modifier.padding(top = 4.dp),
            )
        }

        val err = error
        if (err != null) {
            Spacer(Modifier.height(14.dp))
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(14.dp)) {
                    Text(
                        text = stringResource(R.string.error_title),
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.error,
                    )
                    Text(err, style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 6.dp))
                }
            }
        }

        val r = report
        if (r != null && r.ok) {
            Spacer(Modifier.height(14.dp))
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(14.dp)) {
                    Text(
                        text = stringResource(R.string.report_title),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        text = stringResource(
                            R.string.report_body,
                            r.titleId,
                            r.keyGeneration,
                            humanSize(r.size),
                            r.entries.size,
                            r.keysCount,
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 6.dp),
                    )
                    if (r.missingKeys.isNotEmpty()) {
                        HorizontalDivider(modifier = Modifier.padding(vertical = 10.dp))
                        Text(
                            text = stringResource(
                                R.string.missing_keys_title,
                                r.keyGeneration,
                                r.keysMaxGeneration,
                            ),
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                        Text(
                            text = stringResource(R.string.missing_keys_body),
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(top = 4.dp),
                        )
                        r.missingKeys.forEach { key ->
                            Text(
                                text = "• $key",
                                fontFamily = FontFamily.Monospace,
                                style = MaterialTheme.typography.bodySmall,
                                modifier = Modifier.padding(top = 2.dp),
                            )
                        }
                        Text(
                            text = stringResource(R.string.missing_keys_stop),
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                }
            }
        }

        val done = imported
        if (done != null) {
            Spacer(Modifier.height(14.dp))
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(14.dp)) {
                    Text(
                        text = stringResource(R.string.import_done_title),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        text = stringResource(
                            R.string.import_done_body,
                            done.name,
                            humanSize(done.sizeBytes),
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 6.dp),
                    )
                    Text(
                        text = stringResource(R.string.prepare_pending),
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 6.dp),
                    )
                    Button(onClick = onDone, modifier = Modifier.padding(top = 10.dp)) {
                        Text(stringResource(R.string.open_library))
                    }
                }
            }
        }

        Spacer(Modifier.height(20.dp))
        Surface(color = MaterialTheme.colorScheme.surfaceVariant, shape = MaterialTheme.shapes.small) {
            Text(
                text = stringResource(R.string.import_scope_note),
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(12.dp),
            )
        }
    }
}
