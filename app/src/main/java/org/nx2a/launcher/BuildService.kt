package org.nx2a.launcher

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/**
 * Keeps the on-device build alive while the app is in the background and shows
 * its stage and percentage in a foreground notification, with a cancel action.
 */
class BuildService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var titleId: String = ""
    private var gameName: String = ""
    private var lastNotifiedPercent = -1
    private var lastNotifiedStage = ""
    private var lastNotifyAt = 0L

    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(
            CHANNEL_ID,
            getString(R.string.build_channel_name),
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = getString(R.string.build_channel_description)
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CANCEL -> {
                Logs.add("build", "cancel requested")
                val current = BuildManager.state.value
                if (current != null && current.running) {
                    BuildManager.publish(
                        current.copy(
                            running = false,
                            cancelled = true,
                            finishedAt = System.currentTimeMillis(),
                        ),
                    )
                }
                job?.cancel()
                val manager = getSystemService(NotificationManager::class.java)
                manager.notify(NOTIFICATION_ID, notification(current ?: emptySnapshot(), final = true))
                ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_DETACH)
                stopSelf()
                return START_NOT_STICKY
            }

            ACTION_START -> {
                if (job?.isActive == true) return START_NOT_STICKY
                titleId = intent.getStringExtra(EXTRA_TITLE_ID).orEmpty()
                gameName = intent.getStringExtra(EXTRA_GAME_NAME).orEmpty()
                if (titleId.isBlank()) {
                    stopSelf()
                    return START_NOT_STICKY
                }
                val initial = emptySnapshot().copy(
                    titleId = titleId,
                    gameName = gameName,
                    running = true,
                    startedAt = System.currentTimeMillis(),
                )
                BuildManager.publish(initial)
                startInForeground(initial)
                acquireWakeLock()
                job = scope.launch {
                    try {
                        val game = AppRepository(applicationContext).games.first()
                            .firstOrNull { it.titleId == titleId }
                        if (game == null) {
                            val failed = initial.copy(
                                running = false,
                                failed = true,
                                errorCode = "ERR_GAME",
                                error = getString(R.string.build_err_game_missing),
                                finishedAt = System.currentTimeMillis(),
                            )
                            BuildManager.publish(failed)
                            finalise(failed)
                            return@launch
                        }
                        val snapshot = PortPipeline(applicationContext).run(game) { update ->
                            publish(update)
                        }
                        finalise(snapshot)
                    } catch (_: CancellationException) {
                        val current = BuildManager.state.value ?: initial
                        finalise(current.copy(running = false, cancelled = true))
                    } catch (t: Throwable) {
                        val failed = (BuildManager.state.value ?: initial).copy(
                            running = false,
                            failed = true,
                            errorCode = "ERR_BUILD",
                            error = t.message ?: getString(R.string.err_generic),
                            finishedAt = System.currentTimeMillis(),
                        )
                        BuildManager.publish(failed)
                        finalise(failed)
                    }
                }
            }
        }
        return START_NOT_STICKY
    }

    private fun emptySnapshot() = BuildSnapshot(titleId = titleId, gameName = gameName)

    private fun publish(snapshot: BuildSnapshot) {
        BuildManager.publish(snapshot)
        val now = System.currentTimeMillis()
        val percent = (snapshot.overall * 100).toInt()
        val active = snapshot.stages.firstOrNull { it.state == StageState.ACTIVE }
        val stageKey = active?.id?.name ?: snapshot.stages.lastOrNull { it.state == StageState.DONE }?.id?.name.orEmpty()
        if (percent != lastNotifiedPercent || stageKey != lastNotifiedStage || now - lastNotifyAt > 1500) {
            lastNotifiedPercent = percent
            lastNotifiedStage = stageKey
            lastNotifyAt = now
            getSystemService(NotificationManager::class.java)
                .notify(NOTIFICATION_ID, notification(snapshot, final = false))
        }
    }

    private fun finalise(snapshot: BuildSnapshot) {
        BuildManager.publish(snapshot)
        val manager = getSystemService(NotificationManager::class.java)
        manager.notify(NOTIFICATION_ID, notification(snapshot, final = true))
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_DETACH)
        releaseWakeLock()
        stopSelf()
    }

    private fun startInForeground(snapshot: BuildSnapshot) {
        ServiceCompat.startForeground(
            this,
            NOTIFICATION_ID,
            notification(snapshot, final = false),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC,
        )
    }

    private fun notification(snapshot: BuildSnapshot, final: Boolean): Notification {
        val open = Intent(this, BuildActivity::class.java)
            .putExtra(BuildActivity.EXTRA_TITLE_ID, snapshot.titleId.ifBlank { titleId })
            .putExtra(BuildActivity.EXTRA_GAME_NAME, snapshot.gameName.ifBlank { gameName })
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        val contentIntent = PendingIntent.getActivity(
            this, 1, open,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val cancelIntent = PendingIntent.getService(
            this, 2,
            Intent(this, BuildService::class.java).setAction(ACTION_CANCEL),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )

        val active = snapshot.stages.firstOrNull { it.state == StageState.ACTIVE }
        val title = when {
            final && snapshot.cancelled -> getString(R.string.build_notif_cancelled)
            final && snapshot.failed -> getString(R.string.build_notif_failed)
            final && snapshot.finished -> getString(R.string.build_notif_done)
            else -> getString(R.string.build_notif_running, snapshot.gameName)
        }
        val text = when {
            final && snapshot.failed -> snapshot.error.take(90)
            final && snapshot.cancelled -> getString(R.string.build_notif_cancelled_body)
            final && snapshot.finished -> getString(
                R.string.build_notif_done_body,
                humanSize(snapshot.extractedBytes),
            )
            active != null -> "${stageName(active.id)} · ${active.detail}".take(90)
            else -> getString(R.string.build_notif_preparing)
        }

        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_build)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(contentIntent)
            .setOnlyAlertOnce(true)
            .setOngoing(!final)
            .setAutoCancel(final)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_PROGRESS)

        if (!final) {
            builder.setProgress(100, (snapshot.overall * 100).toInt(), snapshot.overall <= 0f)
            builder.addAction(
                R.drawable.ic_build,
                getString(R.string.cancel),
                cancelIntent,
            )
        } else if (snapshot.finished) {
            builder.addAction(
                R.drawable.ic_build,
                getString(R.string.build_open),
                contentIntent,
            )
        }
        return builder.build()
    }

    private fun stageName(id: BuildStageId): String = getString(
        when (id) {
            BuildStageId.VERIFY -> R.string.stage_verify
            BuildStageId.KEYS -> R.string.stage_keys
            BuildStageId.EXTRACT -> R.string.stage_extract
            BuildStageId.REPORT -> R.string.stage_report
            BuildStageId.RUNTIME -> R.string.stage_runtime
        },
    )

    private fun acquireWakeLock() {
        if (wakeLock?.isHeld == true) return
        val power = getSystemService(PowerManager::class.java)
        wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "nx2a:build").apply {
            setReferenceCounted(false)
            acquire(2 * 60 * 60 * 1000L)
        }
    }

    private fun releaseWakeLock() {
        runCatching { if (wakeLock?.isHeld == true) wakeLock?.release() }
        wakeLock = null
    }

    override fun onDestroy() {
        scope.cancel()
        releaseWakeLock()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        const val ACTION_START = "org.nx2a.launcher.BUILD_START"
        const val ACTION_CANCEL = "org.nx2a.launcher.BUILD_CANCEL"
        const val EXTRA_TITLE_ID = "title_id"
        const val EXTRA_GAME_NAME = "game_name"
        private const val CHANNEL_ID = "nx2a-build"
        private const val NOTIFICATION_ID = 4201

        fun start(context: Context, titleId: String, gameName: String) {
            val intent = Intent(context, BuildService::class.java)
                .setAction(ACTION_START)
                .putExtra(EXTRA_TITLE_ID, titleId)
                .putExtra(EXTRA_GAME_NAME, gameName)
            ContextCompat.startForegroundService(context, intent)
        }

        fun cancel(context: Context) {
            val intent = Intent(context, BuildService::class.java).setAction(ACTION_CANCEL)
            runCatching { context.startService(intent) }
        }
    }
}