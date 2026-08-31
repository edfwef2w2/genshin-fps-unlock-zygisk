package io.github.unlockfps.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.github.unlockfps.BuildConfig

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AboutScreen(onBack: () -> Unit) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("About") },
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
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("Genshin FPS Unlock Zygisk", style = MaterialTheme.typography.headlineSmall)
            Text("Version ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
            Text(
                "Android Zygisk port of 34736384/genshin-fps-unlock. The module writes the target frame rate inside the game process.",
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                "HoYoverse is aware of FPS unlock on PC. Using only FPS unlock is widely reported as safe there. Android root hiding is a separate risk: if the game detects Magisk/Zygisk it may refuse to launch. Use a hide stack you already trust. Extra plugins are at your own risk.",
                style = MaterialTheme.typography.bodyMedium
            )
            Text("Apache License 2.0", style = MaterialTheme.typography.bodySmall)
            Text("Upstream: github.com/34736384/genshin-fps-unlock (MIT)", style = MaterialTheme.typography.bodySmall)
        }
    }
}
