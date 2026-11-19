package org.nx2a.launcher

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.Button
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun LibraryScreen(
    games: List<GameEntry>,
    onImport: () -> Unit,
    onPlay: (GameEntry) -> Unit,
    onDelete: (GameEntry) -> Unit,
) {
    var pendingDelete by remember { mutableStateOf<GameEntry?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
    ) {
        Text(
            text = stringResource(R.string.tab_library),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.SemiBold,
        )
        Text(
            text = stringResource(R.string.library_subtitle),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(top = 4.dp),
        )
        Spacer(Modifier.height(12.dp))

        if (games.isEmpty()) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text(
                        text = stringResource(R.string.library_empty_title),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        text = stringResource(R.string.library_empty_body),
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 8.dp),
                    )
                    Text(
                        text = stringResource(R.string.legal_note),
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 8.dp),
                    )
                    Button(onClick = onImport, modifier = Modifier.padding(top = 12.dp)) {
                        Text(stringResource(R.string.import_action))
                    }
                }
            }
        } else {
            LazyColumn(
                verticalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(games, key = { it.titleId }) { game ->
                    GameCard(
                        game = game,
                        onPlay = { onPlay(game) },
                        onDelete = { pendingDelete = game },
                    )
                }
            }
        }
    }

    val victim = pendingDelete
    if (victim != null) {
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text(stringResource(R.string.delete_title)) },
            text = { Text(stringResource(R.string.delete_body, victim.name)) },
            confirmButton = {
                TextButton(onClick = {
                    onDelete(victim)
                    pendingDelete = null
                }) { Text(stringResource(R.string.delete_confirm)) }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete = null }) {
                    Text(stringResource(R.string.cancel))
                }
            },
        )
    }
}

@Composable
private fun GameCard(game: GameEntry, onPlay: () -> Unit, onDelete: () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp)) {
            Row(Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text(game.name, style = MaterialTheme.typography.titleMedium)
                    Text(
                        text = game.titleId,
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                    )
                }
                Surface(
                    color = MaterialTheme.colorScheme.secondaryContainer,
                    shape = MaterialTheme.shapes.small,
                ) {
                    Text(
                        text = stringResource(
                            if (game.status == GameEntry.STATUS_READY) R.string.status_ready
                            else R.string.status_validated,
                        ),
                        style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                    )
                }
            }
            Text(
                text = stringResource(
                    R.string.game_details,
                    humanSize(game.sizeBytes),
                    game.keyGeneration,
                    SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)
                        .format(Date(game.importedAt)),
                ),
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(top = 6.dp),
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onPlay) { Text(stringResource(R.string.play)) }
                OutlinedButton(onClick = onDelete) {
                    Text(stringResource(R.string.delete_confirm))
                }
            }
        }
    }
}
