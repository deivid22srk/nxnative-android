package org.nx2a.launcher

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Dark = darkColorScheme(
    primary = Color(0xFF8ED0FF),
    secondary = Color(0xFFB6C8DA),
    background = Color(0xFF0B1015),
    surface = Color(0xFF121A21),
)

private val Light = lightColorScheme(
    primary = Color(0xFF0A5C8C),
    secondary = Color(0xFF4B6072),
)

@Composable
fun NxTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) Dark else Light,
        content = content,
    )
}
