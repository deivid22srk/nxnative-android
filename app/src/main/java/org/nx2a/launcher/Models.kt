package org.nx2a.launcher

import org.json.JSONArray
import org.json.JSONObject

/** Result of inspecting one NSP plus cross-checking it against prod.keys. */
data class ImportReport(
    val ok: Boolean,
    val code: String,
    val error: String,
    val titleId: String,
    val keyGeneration: Int,
    val ticket: String,
    val size: Long,
    val entries: List<NspEntry>,
    val missingKeys: List<String>,
    val keysCount: Int,
    val keysMaxGeneration: Int,
    val supported: Boolean,
)

data class NspEntry(
    val name: String,
    val kind: String,
    val size: Long,
    /** Absolute offset of the payload inside the NSP file. */
    val offset: Long = 0,
)

fun parseImportReport(json: String): ImportReport = try {
    val o = JSONObject(json)
    val entries = mutableListOf<NspEntry>()
    val arr = o.optJSONArray("entries") ?: JSONArray()
    for (i in 0 until arr.length()) {
        val e = arr.getJSONObject(i)
        entries += NspEntry(
                e.optString("name"),
                e.optString("kind"),
                e.optLong("size"),
                e.optLong("offset"),
            )
    }
    val missing = mutableListOf<String>()
    val marr = o.optJSONArray("missingKeys") ?: JSONArray()
    for (i in 0 until marr.length()) missing += marr.getString(i)
    ImportReport(
        ok = o.optBoolean("ok", false),
        code = o.optString("code", "ERR_JSON"),
        error = o.optString("error", ""),
        titleId = o.optString("titleId", ""),
        keyGeneration = o.optInt("keyGeneration", -1),
        ticket = o.optString("ticket", ""),
        size = o.optLong("size", 0),
        entries = entries,
        missingKeys = missing,
        keysCount = o.optInt("keysCount", 0),
        keysMaxGeneration = o.optInt("keysMaxGeneration", -1),
        supported = o.optBoolean("supported", false),
    )
} catch (t: Throwable) {
    ImportReport(false, "ERR_JSON", t.message ?: "invalid native result", "", -1, "", 0,
        emptyList(), emptyList(), 0, -1, false)
}

data class GameEntry(
    val titleId: String,
    val name: String,
    val sizeBytes: Long,
    val keyGeneration: Int,
    val importedAt: Long,
    val status: String,
    val nspFile: String,
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("titleId", titleId)
        put("name", name)
        put("sizeBytes", sizeBytes)
        put("keyGeneration", keyGeneration)
        put("importedAt", importedAt)
        put("status", status)
        put("nspFile", nspFile)
    }

    companion object {
        const val STATUS_VALIDATED = "validated"
        const val STATUS_READY = "ready"

        fun fromJson(o: JSONObject) = GameEntry(
            titleId = o.optString("titleId"),
            name = o.optString("name"),
            sizeBytes = o.optLong("sizeBytes"),
            keyGeneration = o.optInt("keyGeneration", -1),
            importedAt = o.optLong("importedAt"),
            status = o.optString("status", STATUS_VALIDATED),
            nspFile = o.optString("nspFile"),
        )
    }
}

data class GameSettings(
    val resolutionScale: Float = 1.0f,
    val fpsTarget: Int = 60,
    val touchControls: Boolean = true,
    val showFps: Boolean = true,
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("resolutionScale", resolutionScale.toDouble())
        put("fpsTarget", fpsTarget)
        put("touchControls", touchControls)
        put("showFps", showFps)
    }

    companion object {
        fun fromJson(o: JSONObject) = GameSettings(
            resolutionScale = o.optDouble("resolutionScale", 1.0).toFloat(),
            fpsTarget = o.optInt("fpsTarget", 60),
            touchControls = o.optBoolean("touchControls", true),
            showFps = o.optBoolean("showFps", true),
        )
    }
}

fun humanSize(bytes: Long): String {
    if (bytes <= 0) return "0 B"
    val units = arrayOf("B", "KB", "MB", "GB", "TB")
    var value = bytes.toDouble()
    var unit = 0
    while (value >= 1024.0 && unit < units.lastIndex) {
        value /= 1024.0
        unit++
    }
    return if (unit == 0) "${bytes} B" else String.format("%.2f %s", value, units[unit])
}
