package org.nx2a.launcher

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
fun LogsScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var lines by remember { mutableStateOf(Logs.snapshot()) }
    var message by remember { mutableStateOf<String?>(null) }

    val export = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("text/plain"),
    ) { uri ->
        if (uri != null) {
            scope.launch {
                message = try {
                    val count = Logs.export(context, uri)
                    context.getString(R.string.export_ok, count)
                } catch (t: Throwable) {
                    context.getString(R.string.export_failed, t.message ?: "?")
                }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
    ) {
        Text(
            text = stringResource(R.string.tab_logs),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.SemiBold,
        )
        Text(
            text = stringResource(R.string.logs_note),
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(top = 4.dp),
        )
        Row(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            modifier = Modifier.padding(vertical = 10.dp),
        ) {
            Button(onClick = {
                export.launch("nxnative-diagnostics.txt")
            }) { Text(stringResource(R.string.export_logs)) }
            OutlinedButton(onClick = {
                Logs.clear()
                lines = Logs.snapshot()
            }) { Text(stringResource(R.string.clear_logs)) }
            OutlinedButton(onClick = { lines = Logs.snapshot() }) {
                Text(stringResource(R.string.refresh))
            }
        }
        message?.let {
            Text(it, style = MaterialTheme.typography.bodySmall)
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            items(lines) { line ->
                Text(
                    text = line,
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}
