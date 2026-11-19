package org.nx2a.launcher

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class BuildStageId { VERIFY, KEYS, EXTRACT, SHADERS, REPORT, RUNTIME }

enum class StageState { PENDING, ACTIVE, DONE, FAILED, SKIPPED }

data class BuildStageState(
    val id: BuildStageId,
    val state: StageState = StageState.PENDING,
    val detail: String = "",
    val progress: Float = 0f,
)

data class BuildSnapshot(
    val titleId: String,
    val gameName: String,
    val running: Boolean = false,
    val finished: Boolean = false,
    val failed: Boolean = false,
    val cancelled: Boolean = false,
    val errorCode: String = "",
    val error: String = "",
    val stages: List<BuildStageState> = BuildStageId.entries.map { BuildStageState(it) },
    val overall: Float = 0f,
    val extractedBytes: Long = 0,
    val totalBytes: Long = 0,
    val keysVerified: Boolean = false,
    val headerMagic: String = "",
    val contentType: Int = -1,
    val reportPath: String = "",
    val startedAt: Long = 0,
    val finishedAt: Long = 0,
) {
    val runtimeIncluded: Boolean get() = false
    fun stage(id: BuildStageId): BuildStageState =
        stages.firstOrNull { it.id == id } ?: BuildStageState(id)
}

/** The single source of truth the UI observes; the service writes it. */
object BuildManager {
    private val _state = MutableStateFlow<BuildSnapshot?>(null)
    val state: StateFlow<BuildSnapshot?> = _state.asStateFlow()

    fun publish(snapshot: BuildSnapshot) {
        _state.value = snapshot
    }

    fun isRunning(titleId: String): Boolean =
        _state.value?.let { it.running && it.titleId == titleId } == true

    fun start(context: android.content.Context, game: GameEntry) {
        BuildService.start(context, game.titleId, game.name)
    }

    fun cancel(context: android.content.Context) {
        BuildService.cancel(context)
    }
}