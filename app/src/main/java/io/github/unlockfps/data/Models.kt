package io.github.unlockfps.data

data class UnlockConfig(
    val fps: Int = 120,
    val powerSave: Boolean = false,
    val delaySeconds: Int = 8,
    val autoStart: Boolean = false,
    val packages: List<String> = defaultPackages,
) {
    fun toJson(): String {
        val pkgs = packages.joinToString(",") { "\"$it\"" }
        return """
            {
              "fps": $fps,
              "powerSave": $powerSave,
              "delaySeconds": $delaySeconds,
              "autoStart": $autoStart,
              "packages": [$pkgs]
            }
        """.trimIndent()
    }

    companion object {
        val defaultPackages = listOf(
            "com.miHoYo.Yuanshen",
            "com.miHoYo.GenshinImpact",
            "com.miHoYo.ys",
        )

        fun fromJson(raw: String): UnlockConfig {
            fun intVal(key: String, default: Int): Int {
                val regex = """"$key"\s*:\s*(-?\d+)""".toRegex()
                return regex.find(raw)?.groupValues?.get(1)?.toIntOrNull() ?: default
            }
            fun boolVal(key: String, default: Boolean): Boolean {
                val regex = """"$key"\s*:\s*(true|false)""".toRegex()
                return regex.find(raw)?.groupValues?.get(1)?.toBooleanStrictOrNull() ?: default
            }
            val pkgRegex = """"packages"\s*:\s*\[(.*?)\]""".toRegex(RegexOption.DOT_MATCHES_ALL)
            val pkgs = pkgRegex.find(raw)?.groupValues?.get(1)
                ?.split(",")
                ?.map { it.trim().trim('"') }
                ?.filter { it.isNotEmpty() }
                .orEmpty()
            return UnlockConfig(
                fps = intVal("fps", 120).coerceIn(10, 240),
                powerSave = boolVal("powerSave", false),
                delaySeconds = intVal("delaySeconds", 8).coerceIn(0, 60),
                autoStart = boolVal("autoStart", false),
                packages = pkgs.ifEmpty { defaultPackages },
            )
        }
    }
}

enum class ModulePresence {
    NoRoot,
    Missing,
    Installed,
}

data class UnlockStatus(
    val presence: ModulePresence = ModulePresence.Missing,
    val state: String = "none",
    val mode: String = "unknown",
    val pid: Int = 0,
    val fps: Int = 0,
    val message: String = "",
) {
    val ready: Boolean get() = state == "ready"
    val error: Boolean get() = state == "error"
}

data class GamePackage(
    val id: String,
    val label: String,
)
