package org.nx2a.launcher

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen(
    settings: GameSettings,
    onChange: (GameSettings) -> Unit,
    games: List<GameEntry>,
    importer: ImportRepository,
    onDeleteAll: () -> Unit,
) {
    var confirmDelete by remember { mutableStateOf(false) }
    val used = games.sumOf { it.sizeBytes }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        Text(
            text = stringResource(R.string.tab_settings),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.SemiBold,
        )

        Card(Modifier.fillMaxWidth().padding(top = 14.dp)) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = stringResource(R.string.settings_resolution),
                    style = MaterialTheme.typography.titleSmall,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    listOf(0.5f to "50%", 0.75f to "75%", 1.0f to "100%").forEach { (value, label) ->
                        FilterChip(
                            selected = settings.resolutionScale == value,
                            onClick = { onChange(settings.copy(resolutionScale = value)) },
                            label = { Text(label) },
                        )
                    }
                }
                Text(
                    text = stringResource(R.string.settings_fps),
                    style = MaterialTheme.typography.titleSmall,
                    modifier = Modifier.padding(top = 6.dp),
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    listOf(30, 60).forEach { value ->
                        FilterChip(
                            selected = settings.fpsTarget == value,
                            onClick = { onChange(settings.copy(fpsTarget = value)) },
                            label = { Text("$value fps") },
                        )
                    }
                }
            }
        }

        Card(Modifier.fillMaxWidth().padding(top = 12.dp)) {
            Column(Modifier.padding(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = stringResource(R.string.settings_touch),
                        modifier = Modifier.weight(1f),
                    )
                    Switch(
                        checked = settings.touchControls,
                        onCheckedChange = { onChange(settings.copy(touchControls = it)) },
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = stringResource(R.string.settings_show_fps),
                        modifier = Modifier.weight(1f),
                    )
                    Switch(
                        checked = settings.showFps,
                        onCheckedChange = { onChange(settings.copy(showFps = it)) },
                    )
                }
            }
        }

        Card(Modifier.fillMaxWidth().padding(top = 12.dp)) {
            Column(Modifier.padding(14.dp)) {
                Text(
                    text = stringResource(R.string.settings_storage),
                    style = MaterialTheme.typography.titleSmall,
                )
                Text(
                    text = stringResource(
                        R.string.storage_used,
                        humanSize(used),
                        humanSize(importer.availableBytes()),
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 4.dp),
                )
                OutlinedButton(
                    onClick = { confirmDelete = true },
                    enabled = games.isNotEmpty(),
                    modifier = Modifier.padding(top = 10.dp),
                ) { Text(stringResource(R.string.delete_all)) }
            }
        }

        Text(
            text = stringResource(R.string.settings_runtime_note),
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(top = 14.dp),
        )
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text(stringResource(R.string.delete_all_title)) },
            text = { Text(stringResource(R.string.delete_all_body)) },
            confirmButton = {
                TextButton(onClick = {
                    onDeleteAll()
                    confirmDelete = false
                }) { Text(stringResource(R.string.delete_confirm)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = false }) {
                    Text(stringResource(R.string.cancel))
                }
            },
        )
    }
}
