package org.nx2a.launcher

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
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
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
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
import kotlinx.coroutines.launch

@Composable
fun PortScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var ready by remember { mutableStateOf(PortData.ready()) }
    var hasAccess by remember {
        mutableStateOf(
            Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager(),
        )
    }
    var importing by remember { mutableStateOf(false) }
    var progress by remember { mutableStateOf(0L) }
    var entries by remember { mutableStateOf(0) }
    var message by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        ready = PortData.ready()
        hasAccess = Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()
    }

    fun requestAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            runCatching {
                context.startActivity(
                    Intent(
                        Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:${context.packageName}"),
                    ),
                )
            }.onFailure {
                context.startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
            }
        }
    }

    val pickZip = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null && !importing) {
            importing = true
            message = null
            progress = 0
            entries = 0
            scope.launch {
                try {
                    PortData.importZip(context, uri) { bytes, files ->
                        progress = bytes
                        entries = files
                    }
                    ready = PortData.ready()
                    message = context.getString(R.string.port_import_ok, entries, humanSize(progress))
                } catch (t: Throwable) {
                    message = context.getString(R.string.port_import_failed, t.message ?: "?")
                } finally {
                    importing = false
                }
            }
        }
    }

    val exportLog = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("text/plain"),
    ) { uri ->
        if (uri != null) {
            scope.launch {
                try {
                    val text = buildString {
                        append("nxnative-android HD port log\n\n")
                        PortData.logFile()?.let { append(it.readText()) }
                        PortData.previousLogFile()?.let { append("\n--- previous ---\n"); append(it.readText()) }
                        if (isEmpty()) append("no log.txt yet")
                    }
                    context.contentResolver.openOutputStream(uri)?.use { it.write(text.toByteArray()) }
                    message = context.getString(R.string.export_ok, text.lines().size)
                } catch (t: Throwable) {
                    message = context.getString(R.string.export_failed, t.message ?: "?")
                }
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
            text = stringResource(R.string.port_title),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.SemiBold,
        )
        Text(
            text = stringResource(R.string.port_body),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(top = 6.dp),
        )

        Card(Modifier.fillMaxWidth().padding(top = 14.dp)) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = if (ready) {
                        stringResource(R.string.port_ready)
                    } else {
                        stringResource(R.string.port_missing)
                    },
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = "/sdcard/HogwartsLegacy-data",
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                )

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (!hasAccess) {
                        Button(onClick = { requestAccess() }) {
                            Text(stringResource(R.string.port_grant_access))
                        }
                    } else {
                        OutlinedButton(
                            onClick = { pickZip.launch(arrayOf("application/zip", "*/*")) },
                            enabled = !importing,
                        ) { Text(stringResource(R.string.port_import)) }
                    }
                }
                if (importing) {
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    Text(
                        text = "${humanSize(progress)} · $entries " +
                            stringResource(R.string.port_files),
                        style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
                Button(
                    onClick = {
                        runCatching { context.startActivity(Intent(context, PortActivity::class.java)) }
                            .onFailure {
                                message = context.getString(R.string.port_launch_failed, it.message ?: "?")
                            }
                    },
                    enabled = ready && hasAccess,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.port_play)) }

                OutlinedButton(
                    onClick = { exportLog.launch("hogl-log.txt") },
                    enabled = PortData.logFile() != null,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.port_export_log)) }
            }
        }

        message?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(top = 10.dp),
            )
        }

        Spacer(Modifier.height(12.dp))
        Text(
            text = stringResource(R.string.port_note),
            style = MaterialTheme.typography.bodySmall,
        )
    }
}