package io.github.unlockfps.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.github.unlockfps.UiState
import io.github.unlockfps.data.ConfigRepository

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    state: UiState,
    onTogglePackage: (String, Boolean) -> Unit,
    onDelay: (Int) -> Unit,
    onBack: () -> Unit,
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
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
            Text("Regions", style = MaterialTheme.typography.titleMedium)
            ConfigRepository.knownGames.forEach { game ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Checkbox(
                        checked = game.id in state.config.packages,
                        onCheckedChange = { onTogglePackage(game.id, it) }
                    )
                    Column {
                        Text(game.label)
                        Text(game.id, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
            Text("Inject delay: ${state.config.delaySeconds}s", style = MaterialTheme.typography.titleMedium)
            Text(
                "Wait after libil2cpp.so loads so IL2CPP can finish initializing.",
                style = MaterialTheme.typography.bodySmall
            )
            Slider(
                value = state.config.delaySeconds.toFloat(),
                onValueChange = { onDelay(it.toInt()) },
                valueRange = 0f..30f,
                steps = 29
            )
        }
    }
}
