package org.nx2a.launcher

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
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
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.launch

class BuildActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val titleId = intent.getStringExtra(EXTRA_TITLE_ID).orEmpty()
        val gameName = intent.getStringExtra(EXTRA_GAME_NAME).orEmpty()
        setContent {
            NxTheme {
                BuildScreen(titleId = titleId, gameName = gameName, onExit = { finish() })
            }
        }
    }

    companion object {
        const val EXTRA_TITLE_ID = "title_id"
        const val EXTRA_GAME_NAME = "game_name"
    }
}

@Composable
private fun BuildScreen(titleId: String, gameName: String, onExit: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val state by BuildManager.state.collectAsStateWithLifecycle()
    var askedPermission by remember { mutableStateOf(false) }
    var exportMessage by remember { mutableStateOf<String?>(null) }

    fun start() {
        BuildService.start(context, titleId, gameName)
    }

    val permission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { start() }

    LaunchedEffect(titleId, state?.titleId, state?.running) {
        if (askedPermission) return@LaunchedEffect
        val current = BuildManager.state.value
        if (current != null && current.running && current.titleId != titleId) {
            askedPermission = true
            return@LaunchedEffect
        }
        if (current != null && current.titleId == titleId) {
            askedPermission = true
            return@LaunchedEffect
        }
        askedPermission = true
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            permission.launch(Manifest.permission.POST_NOTIFICATIONS)
        } else {
            start()
        }
    }

    val create = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/zip"),
    ) { uri ->
        if (uri != null) {
            scope.launch {
                exportMessage = try {
                    val result = BuildExport.export(context, uri, titleId)
                    context.getString(R.string.build_export_ok, result.files, humanSize(result.bytes))
                } catch (t: Throwable) {
                    context.getString(R.string.export_failed, t.message ?: "?")
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
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Column(Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.build_title),
                    style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    text = gameName.ifBlank { titleId },
                    style = MaterialTheme.typography.bodyMedium,
                )
                Text(
                    text = titleId,
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                )
            }
            TextButton(onClick = onExit) { Text(stringResource(R.string.build_close)) }
        }

        Spacer(Modifier.height(12.dp))

        val snapshot = state?.takeIf { it.titleId == titleId }
        val otherRunning = state?.takeIf { it.running && it.titleId != titleId }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(14.dp)) {
                Text(
                    text = when {
                        otherRunning != null -> stringResource(
                            R.string.build_other_running,
                            otherRunning.gameName,
                        )
                        snapshot == null -> stringResource(R.string.build_idle)
                        snapshot.running -> stringResource(R.string.build_running, snapshot.gameName)
                        snapshot.finished -> stringResource(R.string.build_done_title)
                        snapshot.failed -> stringResource(R.string.build_failed_title)
                        snapshot.cancelled -> stringResource(R.string.build_cancelled_title)
                        else -> stringResource(R.string.build_idle)
                    },
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = stringResource(R.string.build_honest_note),
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 6.dp),
                )

                if (snapshot != null) {
                    Spacer(Modifier.height(12.dp))
                    LinearProgressIndicator(
                        progress = { snapshot.overall.coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Text(
                        text = "${(snapshot.overall * 100).toInt()}% · " +
                            "${humanSize(snapshot.extractedBytes)} / ${humanSize(snapshot.totalBytes)}",
                        style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                    if (snapshot.keysVerified) {
                        Text(
                            text = stringResource(
                                R.string.build_keys_ok,
                                snapshot.headerMagic.ifBlank { "NCA" },
                                snapshot.contentType,
                            ),
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(top = 2.dp),
                        )
                    }

                    HorizontalDivider(modifier = Modifier.padding(vertical = 10.dp))
                    snapshot.stages.forEach { stage ->
                        StageRow(stage)
                    }

                    snapshot.error.takeIf { it.isNotBlank() }?.let {
                        Text(
                            text = "${snapshot.errorCode}: $it",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                    if (snapshot.finished) {
                        Text(
                            text = stringResource(R.string.build_no_runtime_warning),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                }

                Spacer(Modifier.height(12.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (snapshot?.running == true) {
                        Button(onClick = { BuildManager.cancel(context) }) {
                            Text(stringResource(R.string.cancel))
                        }
                    } else if (otherRunning == null) {
                        Button(
                            onClick = { start() },
                            enabled = titleId.isNotBlank(),
                        ) {
                            Text(
                                stringResource(
                                    if (snapshot == null) R.string.build_start
                                    else R.string.build_restart,
                                ),
                            )
                        }
                    }
                    if (snapshot != null) {
                        OutlinedButton(
                            onClick = { create.launch("nx2a-$titleId.zip") },
                            enabled = snapshot.finished || snapshot.failed || snapshot.cancelled,
                        ) { Text(stringResource(R.string.build_export)) }
                    }
                }
                exportMessage?.let {
                    Text(
                        text = it,
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 8.dp),
                    )
                }
            }
        }

        Spacer(Modifier.height(12.dp))
        Surface(
            color = MaterialTheme.colorScheme.surfaceVariant,
            shape = MaterialTheme.shapes.small,
        ) {
            Text(
                text = stringResource(R.string.build_scope_note),
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(12.dp),
            )
        }
    }
}

@Composable
private fun StageRow(stage: BuildStageState) {
    val symbol = when (stage.state) {
        StageState.DONE -> "✓"
        StageState.ACTIVE -> "…"
        StageState.FAILED -> "✕"
        StageState.SKIPPED -> "–"
        StageState.PENDING -> "•"
    }
    val color = when (stage.state) {
        StageState.DONE -> MaterialTheme.colorScheme.primary
        StageState.FAILED -> MaterialTheme.colorScheme.error
        StageState.SKIPPED -> MaterialTheme.colorScheme.outline
        else -> MaterialTheme.colorScheme.onSurface
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(
            text = symbol,
            modifier = Modifier.padding(end = 8.dp),
            color = color,
            fontFamily = FontFamily.Monospace,
        )
        Column(Modifier.weight(1f)) {
            Text(
                text = stringResource(
                    when (stage.id) {
                        BuildStageId.VERIFY -> R.string.stage_verify
                        BuildStageId.KEYS -> R.string.stage_keys
                        BuildStageId.EXTRACT -> R.string.stage_extract
                        BuildStageId.REPORT -> R.string.stage_report
                        BuildStageId.RUNTIME -> R.string.stage_runtime
                    },
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = color,
            )
            if (stage.detail.isNotBlank()) {
                Text(
                    text = stage.detail,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.outline,
                )
            }
        }
    }
}