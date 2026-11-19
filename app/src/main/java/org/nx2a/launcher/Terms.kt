package org.nx2a.launcher

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
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
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties

/** Bump when the terms text changes: every user must accept again. */
const val TERMS_VERSION = 1

@Composable
fun TermsDialog(onAccept: () -> Unit, onRefuse: () -> Unit) {
    var checked by remember { mutableStateOf(false) }
    Dialog(
        onDismissRequest = { /* mandatory: not dismissable */ },
        properties = DialogProperties(
            dismissOnBackPress = false,
            dismissOnClickOutside = false,
            usePlatformDefaultWidth = false,
        ),
    ) {
        Surface(modifier = Modifier.fillMaxSize()) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 20.dp, vertical = 16.dp),
            ) {
                Text(
                    text = stringResource(R.string.terms_title),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.SemiBold,
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 12.dp))
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .verticalScroll(rememberScrollState()),
                ) {
                    Text(
                        text = stringResource(R.string.terms_body),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
                HorizontalDivider(modifier = Modifier.padding(vertical = 12.dp))
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Checkbox(checked = checked, onCheckedChange = { checked = it })
                    Text(
                        text = stringResource(R.string.terms_checkbox),
                        style = MaterialTheme.typography.bodyLarge,
                    )
                }
                Spacer(Modifier.height(12.dp))
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    OutlinedButton(
                        onClick = onRefuse,
                        modifier = Modifier.weight(1f),
                    ) { Text(stringResource(R.string.terms_refuse)) }
                    Button(
                        onClick = onAccept,
                        enabled = checked,
                        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 12.dp),
                        modifier = Modifier.weight(2f),
                    ) { Text(stringResource(R.string.terms_accept)) }
                }
            }
        }
    }
}
