package io.github.unlockfps

import android.app.Application
import android.content.Intent
import android.content.pm.PackageManager
import android.view.WindowManager
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import io.github.unlockfps.data.ConfigRepository
import io.github.unlockfps.data.UnlockConfig
import io.github.unlockfps.data.UnlockStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class UiState(
    val config: UnlockConfig = UnlockConfig(),
    val status: UnlockStatus = UnlockStatus(),
    val maxRefreshHz: Int = 60,
    val saveError: String? = null,
    val loaded: Boolean = false,
)

class MainViewModel(app: Application) : AndroidViewModel(app) {
    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    init {
        val hz = maxRefreshRate()
        _state.update { it.copy(maxRefreshHz = hz) }
        viewModelScope.launch {
            refresh()
            val cfg = _state.value.config
            if (cfg.autoStart) {
                launchGame()
            }
            while (isActive) {
                delay(1000)
                val status = withContext(Dispatchers.IO) { ConfigRepository.readStatus() }
                _state.update { it.copy(status = status) }
            }
        }
    }

    private fun maxRefreshRate(): Int {
        val wm = getApplication<Application>().getSystemService(WindowManager::class.java)
        val modes = wm?.defaultDisplay?.supportedModes ?: return 60
        return modes.maxOf { it.refreshRate }.toInt().coerceAtLeast(60)
    }

    fun refresh() {
        viewModelScope.launch {
            val (cfg, status) = withContext(Dispatchers.IO) {
                val presence = ConfigRepository.modulePresence()
                ConfigRepository.readConfig() to ConfigRepository.readStatus(presence)
            }
            _state.update { it.copy(config = cfg, status = status, loaded = true, saveError = null) }
        }
    }

    fun setFps(fps: Int) {
        val next = _state.value.config.copy(fps = fps.coerceIn(10, 240))
        _state.update { it.copy(config = next) }
        persist(next)
    }

    fun setPowerSave(enabled: Boolean) {
        val next = _state.value.config.copy(powerSave = enabled)
        _state.update { it.copy(config = next) }
        persist(next)
    }

    fun setAutoStart(enabled: Boolean) {
        val next = _state.value.config.copy(autoStart = enabled)
        _state.update { it.copy(config = next) }
        persist(next)
    }

    fun setDelay(seconds: Int) {
        val next = _state.value.config.copy(delaySeconds = seconds.coerceIn(0, 60))
        _state.update { it.copy(config = next) }
        persist(next)
    }

    fun togglePackage(id: String, enabled: Boolean) {
        val current = _state.value.config.packages.toMutableList()
        if (enabled && id !in current) current += id
        if (!enabled) current.remove(id)
        val next = _state.value.config.copy(
            packages = current.ifEmpty { UnlockConfig.defaultPackages }
        )
        _state.update { it.copy(config = next) }
        persist(next)
    }

    fun launchGame(): Boolean {
        val pm: PackageManager = getApplication<Application>().packageManager
        val installed = _state.value.config.packages.firstOrNull { pkg ->
            try {
                pm.getLaunchIntentForPackage(pkg) != null
            } catch (_: Exception) {
                false
            }
        } ?: ConfigRepository.knownGames.map { it.id }.firstOrNull { pkg ->
            pm.getLaunchIntentForPackage(pkg) != null
        }
        if (installed == null) {
            return false
        }
        val intent = pm.getLaunchIntentForPackage(installed) ?: return false
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        getApplication<Application>().startActivity(intent)
        return true
    }

    private fun persist(config: UnlockConfig) {
        viewModelScope.launch {
            val ok = withContext(Dispatchers.IO) { ConfigRepository.writeConfig(config) }
            if (!ok) {
                _state.update { it.copy(saveError = "Failed to write config (need root + module)") }
            } else {
                _state.update { it.copy(saveError = null) }
            }
        }
    }
}
