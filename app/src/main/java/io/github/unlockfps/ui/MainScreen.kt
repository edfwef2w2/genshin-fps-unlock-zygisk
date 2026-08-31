package io.github.unlockfps.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.github.unlockfps.UiState
import io.github.unlockfps.data.ModulePresence

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(
    state: UiState,
    onFps: (Int) -> Unit,
    onPowerSave: (Boolean) -> Unit,
    onAutoStart: (Boolean) -> Unit,
    onStartGame: () -> Boolean,
    onOpenSettings: () -> Unit,
    onOpenAbout: () -> Unit,
) {
    var launchFailed by remember { mutableStateOf(false) }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Genshin FPS Unlock Zygisk") },
                actions = {
                    IconButton(onClick = onOpenSettings) {
                        Icon(Icons.Outlined.Settings, contentDescription = "Settings")
                    }
                    IconButton(onClick = onOpenAbout) {
                        Icon(Icons.Outlined.Info, contentDescription = "About")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .padding(16.dp)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            StatusCard(state)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text("Target FPS", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "${state.config.fps}",
                        style = MaterialTheme.typography.displaySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Slider(
                        value = state.config.fps.toFloat(),
                        onValueChange = { onFps(it.toInt()) },
                        valueRange = 10f..240f,
                        steps = 45
                    )
                    if (state.config.fps > state.maxRefreshHz) {
                        Text(
                            "Display reports ${state.maxRefreshHz} Hz. Set a high refresh rate in system settings or the game will still vsync down.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = state.config.powerSave, onCheckedChange = onPowerSave)
                Text("Power save (10 FPS when the game is in the background)")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = state.config.autoStart, onCheckedChange = onAutoStart)
                Text("Start game when this app opens")
            }
            Button(
                modifier = Modifier.fillMaxWidth(),
                onClick = { launchFailed = !onStartGame() }
            ) {
                Text("Start Game")
            }
            if (launchFailed) {
                Text(
                    "Genshin Impact is not installed, or no selected region is present.",
                    color = MaterialTheme.colorScheme.error
                )
            }
            state.saveError?.let {
                Text(it, color = MaterialTheme.colorScheme.error)
            }
            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
private fun StatusCard(state: UiState) {
    val (title, body, error) = when (state.status.presence) {
        ModulePresence.NoRoot -> Triple(
            "No root",
            "Grant root to this app. The Zygisk module cannot run without it.",
            true
        )
        ModulePresence.Missing -> Triple(
            "Module not installed",
            "Flash genshin-fps-unlock-zygisk.zip in Magisk / KernelSU / APatch, enable ZygiskNext, then reboot.",
            true
        )
        ModulePresence.Installed -> when {
            state.status.ready -> Triple(
                "Unlock ready",
                "mode=${state.status.mode} pid=${state.status.pid} fps=${state.status.fps}",
                false
            )
            state.status.error -> Triple(
                "Unlock failed",
                state.status.message.ifEmpty { "Pattern and Unity icall both missed. Check logcat -s UnlockFPS" },
                true
            )
            else -> Triple(
                "Waiting for game",
                "Start Genshin. The module injects on launch and reports ready afterwards.",
                false
            )
        }
    }
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                color = if (error) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
            )
            Text(body, style = MaterialTheme.typography.bodyMedium)
        }
    }
}
