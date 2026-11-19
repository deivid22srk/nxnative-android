package org.nx2a.launcher

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val repository = AppRepository(applicationContext)
        val importer = ImportRepository(applicationContext)
        setContent {
            NxTheme {
                AppRoot(repository, importer, onQuit = { finish() })
            }
        }
    }
}

private enum class Tab(val labelRes: Int) {
    LIBRARY(R.string.tab_library),
    IMPORT(R.string.tab_import),
    SETTINGS(R.string.tab_settings),
    LOGS(R.string.tab_logs),
}

@Composable
private fun AppRoot(
    repository: AppRepository,
    importer: ImportRepository,
    onQuit: () -> Unit,
) {
    var terms by remember { mutableStateOf<AppRepository.TermsState?>(null) }
    LaunchedEffect(Unit) {
        repository.terms.collect { terms = it }
    }

    val current = terms
    if (current == null) {
        Surface(modifier = Modifier.fillMaxSize()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
        }
        return
    }

    if (current.acceptedVersion < TERMS_VERSION) {
        val scope = rememberCoroutineScope()
        TermsDialog(
            onAccept = {
                scope.launch {
                    repository.acceptTerms(TERMS_VERSION, System.currentTimeMillis())
                    Logs.add("terms", "accepted version $TERMS_VERSION")
                }
            },
            onRefuse = onQuit,
        )
        return
    }

    val context = LocalContext.current
    val games by repository.games.collectAsStateWithLifecycle(initialValue = emptyList())
    val settings by repository.settings.collectAsStateWithLifecycle(initialValue = GameSettings())
    var tab by remember { mutableStateOf(Tab.LIBRARY) }
    val scope = rememberCoroutineScope()

    Scaffold(
        bottomBar = {
            NavigationBar {
                Tab.entries.forEach { item ->
                    NavigationBarItem(
                        selected = tab == item,
                        onClick = { tab = item },
                        icon = {
                            Icon(
                                imageVector = when (item) {
                                    Tab.LIBRARY -> Icons.Default.List
                                    Tab.IMPORT -> Icons.Default.Add
                                    Tab.SETTINGS -> Icons.Default.Settings
                                    Tab.LOGS -> Icons.Default.Info
                                },
                                contentDescription = stringResource(item.labelRes),
                            )
                        },
                        label = { Text(stringResource(item.labelRes)) },
                    )
                }
            }
        },
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding)) {
            when (tab) {
                Tab.LIBRARY -> LibraryScreen(
                    games = games,
                    onImport = { tab = Tab.IMPORT },
                    onPlay = { game ->
                        Logs.add("library", "opening player shell for ${game.titleId}")
                        context.startActivity(
                            Intent(context, PlayerActivity::class.java)
                                .putExtra(PlayerActivity.EXTRA_GAME_ID, game.titleId)
                                .putExtra(PlayerActivity.EXTRA_GAME_NAME, game.name),
                        )
                    },
                    onDelete = { game ->
                        scope.launch {
                            importer.deleteGame(game.titleId)
                            repository.removeGame(game.titleId)
                            Logs.add("library", "deleted ${game.titleId}")
                        }
                    },
                )

                Tab.IMPORT -> ImportScreen(
                    repository = repository,
                    importer = importer,
                    onDone = { tab = Tab.LIBRARY },
                )

                Tab.SETTINGS -> SettingsScreen(
                    settings = settings,
                    onChange = { scope.launch { repository.saveSettings(it) } },
                    games = games,
                    importer = importer,
                    onDeleteAll = {
                        scope.launch {
                            games.forEach {
                                importer.deleteGame(it.titleId)
                                repository.removeGame(it.titleId)
                            }
                            Logs.add("settings", "deleted all imported data")
                        }
                    },
                )

                Tab.LOGS -> LogsScreen()
            }
        }
    }
}
