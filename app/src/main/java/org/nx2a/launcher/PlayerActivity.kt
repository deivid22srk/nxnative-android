package org.nx2a.launcher

import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay
import org.json.JSONObject

class PlayerActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val gameId = intent.getStringExtra(EXTRA_GAME_ID).orEmpty()
        val gameName = intent.getStringExtra(EXTRA_GAME_NAME).orEmpty()
        setContent {
            NxTheme {
                PlayerScreen(gameId = gameId, gameName = gameName, onExit = { finish() })
            }
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val device = event.device
        if (device != null &&
            (device.sources and InputDevice.SOURCE_GAMEPAD != 0 ||
                device.sources and InputDevice.SOURCE_JOYSTICK != 0)
        ) {
            Native.rendererInput(
                if (event.action == KeyEvent.ACTION_DOWN) INPUT_KEY_DOWN else INPUT_KEY_UP,
                0f, 0f, event.keyCode,
            )
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onDestroy() {
        Native.rendererStop()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_GAME_ID = "game_id"
        const val EXTRA_GAME_NAME = "game_name"
        const val INPUT_KEY_DOWN = 2
        const val INPUT_KEY_UP = 3
        const val INPUT_BUTTON = 4
    }
}

@Composable
private fun PlayerScreen(gameId: String, gameName: String, onExit: () -> Unit) {
    val context = LocalContext.current
    var status by remember { mutableStateOf(context.getString(R.string.player_starting)) }
    var stats by remember { mutableStateOf<JSONObject?>(null) }
    var surfaceReady by remember { mutableStateOf(false) }

    val surfaceView = remember {
        SurfaceView(context).apply {
            holder.addCallback(object : SurfaceHolder.Callback {
                override fun surfaceCreated(holder: SurfaceHolder) {
                    val result = runCatching {
                        JSONObject(
                            Native.rendererStart(
                                holder.surface,
                                holder.surfaceFrame.width(),
                                holder.surfaceFrame.height(),
                            ),
                        )
                    }.getOrElse { JSONObject().put("ok", false).put("error", it.message) }
                    Logs.add("vk", "surface created: $result")
                    if (result.optBoolean("ok")) {
                        surfaceReady = true
                        status = context.getString(R.string.player_running)
                    } else {
                        surfaceReady = false
                        status = result.optString("error").ifBlank {
                            context.getString(R.string.player_failed)
                        }
                    }
                }

                override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                    Logs.add("vk", "surface changed ${width}x$height format=$format")
                }

                override fun surfaceDestroyed(holder: SurfaceHolder) {
                    surfaceReady = false
                    Native.rendererStop()
                    Logs.add("vk", "surface destroyed")
                }
            })
        }
    }

    LaunchedEffect(Unit) {
        while (true) {
            stats = runCatching { JSONObject(Native.rendererStats()) }.getOrNull()
            delay(1000)
        }
    }

    Box(Modifier.fillMaxSize().background(Color.Black)) {
        AndroidView(factory = { surfaceView }, modifier = Modifier.fillMaxSize())

        Column(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(12.dp)
                .background(Color(0x88000000), MaterialTheme.shapes.small)
                .padding(10.dp),
        ) {
            Text(gameName, color = Color.White, style = MaterialTheme.typography.titleMedium)
            Text(
                gameId,
                color = Color.LightGray,
                fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodySmall,
            )
            val s = stats
            Text(
                text = if (s == null) status else {
                    "GPU: ${s.optString("device")} · Vulkan ${s.optString("api")}\n" +
                        "${s.optInt("width")}x${s.optInt("height")} · ${s.optInt("fps")} fps · " +
                        "${s.optLong("frames")} frames"
                },
                color = Color.LightGray,
                style = MaterialTheme.typography.bodySmall,
            )
            s?.optString("lastInput")?.takeIf { it.isNotBlank() }?.let {
                Text("input: $it", color = Color.Gray, style = MaterialTheme.typography.labelSmall)
            }
            s?.optString("error")?.takeIf { it.isNotBlank() }?.let {
                Text(it, color = Color(0xFFFF8A80), style = MaterialTheme.typography.bodySmall)
            }
        }

        Surface(
            color = Color(0xCC8C1D18),
            shape = MaterialTheme.shapes.small,
            modifier = Modifier.align(Alignment.TopCenter).padding(top = 70.dp),
        ) {
            Text(
                text = stringResource(R.string.player_runtime_notice),
                color = Color.White,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
            )
        }

        Button(
            onClick = onExit,
            modifier = Modifier.align(Alignment.TopEnd).padding(12.dp),
        ) { Text(stringResource(R.string.player_exit)) }

        if (surfaceReady) {
            TouchControls(
                enabled = true,
                modifier = Modifier.align(Alignment.BottomStart).padding(20.dp),
            )
            Column(
                modifier = Modifier.align(Alignment.BottomEnd).padding(20.dp),
                horizontalAlignment = Alignment.End,
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    PadButton("X", 2)
                    PadButton("Y", 3)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    PadButton("B", 1)
                    PadButton("A", 0)
                }
            }
        }
    }
}

@Composable
private fun PadButton(label: String, code: Int) {
    OutlinedButton(
        onClick = {
            Native.rendererInput(PlayerActivity.INPUT_BUTTON, 0f, 0f, code)
            Logs.add("input", "pad $label")
        },
        modifier = Modifier.size(56.dp),
        shape = CircleShape,
    ) { Text(label) }
}

@Composable
private fun TouchControls(enabled: Boolean, modifier: Modifier = Modifier) {
    var knob by remember { mutableStateOf(Offset.Zero) }
    val radius = 56f
    Box(
        modifier = modifier
            .size(140.dp)
            .background(Color(0x55000000), CircleShape)
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                val limit = size.width / 2f
                detectDragGestures(
                    onDragStart = {
                        Native.rendererInput(0, 0f, 0f, 0)
                    },
                    onDragEnd = {
                        knob = Offset.Zero
                        Native.rendererInput(1, 0f, 0f, 0)
                    },
                    onDragCancel = {
                        knob = Offset.Zero
                        Native.rendererInput(1, 0f, 0f, 0)
                    },
                ) { change, drag ->
                    change.consume()
                    val next = knob + drag
                    val length = next.getDistance()
                    knob = if (length > limit) next * (limit / length) else next
                    Native.rendererInput(
                        1,
                        (knob.x / limit).coerceIn(-1f, 1f),
                        (knob.y / limit).coerceIn(-1f, 1f),
                        0,
                    )
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(radius.dp)
                .graphicsLayer {
                    translationX = knob.x
                    translationY = knob.y
                }
                .background(Color(0xAA4FC3F7), CircleShape)
        )
    }
}
