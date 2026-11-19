package org.nx2a.launcher

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import org.json.JSONArray
import org.json.JSONObject

private val Context.dataStore by preferencesDataStore(name = "nxnative_launcher")

class AppRepository(private val context: Context) {

    private object Keys {
        val TERMS_VERSION = intPreferencesKey("terms_version")
        val TERMS_ACCEPTED_AT = longPreferencesKey("terms_accepted_at")
        val GAMES = stringPreferencesKey("games")
        val SETTINGS = stringPreferencesKey("settings")
    }

    data class TermsState(val acceptedVersion: Int, val acceptedAt: Long)

    val terms: Flow<TermsState> = context.dataStore.data.map {
        TermsState(it[Keys.TERMS_VERSION] ?: 0, it[Keys.TERMS_ACCEPTED_AT] ?: 0L)
    }

    suspend fun acceptTerms(version: Int, at: Long) {
        context.dataStore.edit {
            it[Keys.TERMS_VERSION] = version
            it[Keys.TERMS_ACCEPTED_AT] = at
        }
    }

    val games: Flow<List<GameEntry>> = context.dataStore.data.map { prefs ->
        val raw = prefs[Keys.GAMES].orEmpty()
        if (raw.isBlank()) return@map emptyList<GameEntry>()
        try {
            val arr = JSONArray(raw)
            (0 until arr.length()).map { GameEntry.fromJson(arr.getJSONObject(it)) }
        } catch (_: Throwable) {
            emptyList()
        }
    }

    suspend fun upsertGame(entry: GameEntry) {
        val current = games.first().filterNot { it.titleId == entry.titleId } + entry
        context.dataStore.edit {
            it[Keys.GAMES] = JSONArray().apply { current.forEach { g -> put(g.toJson()) } }.toString()
        }
    }

    suspend fun removeGame(titleId: String) {
        val current = games.first().filterNot { it.titleId == titleId }
        context.dataStore.edit {
            it[Keys.GAMES] = JSONArray().apply { current.forEach { g -> put(g.toJson()) } }.toString()
        }
    }

    val settings: Flow<GameSettings> = context.dataStore.data.map { prefs ->
        val raw = prefs[Keys.SETTINGS].orEmpty()
        if (raw.isBlank()) GameSettings() else try {
            GameSettings.fromJson(JSONObject(raw))
        } catch (_: Throwable) {
            GameSettings()
        }
    }

    suspend fun saveSettings(settings: GameSettings) {
        context.dataStore.edit { it[Keys.SETTINGS] = settings.toJson().toString() }
    }
}
