package io.github.unlockfps

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import io.github.unlockfps.ui.AboutScreen
import io.github.unlockfps.ui.MainScreen
import io.github.unlockfps.ui.SettingsScreen
import io.github.unlockfps.ui.theme.UnlockFpsTheme

class MainActivity : ComponentActivity() {
    private val vm: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            UnlockFpsTheme {
                val nav = rememberNavController()
                val state by vm.state.collectAsState()
                NavHost(navController = nav, startDestination = "main") {
                    composable("main") {
                        MainScreen(
                            state = state,
                            onFps = vm::setFps,
                            onPowerSave = vm::setPowerSave,
                            onAutoStart = vm::setAutoStart,
                            onStartGame = vm::launchGame,
                            onOpenSettings = { nav.navigate("settings") },
                            onOpenAbout = { nav.navigate("about") },
                        )
                    }
                    composable("settings") {
                        SettingsScreen(
                            state = state,
                            onTogglePackage = vm::togglePackage,
                            onDelay = vm::setDelay,
                            onBack = { nav.popBackStack() },
                        )
                    }
                    composable("about") {
                        AboutScreen(onBack = { nav.popBackStack() })
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        vm.refresh()
    }
}
