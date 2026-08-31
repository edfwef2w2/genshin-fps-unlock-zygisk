package io.github.unlockfps.data

import android.util.Base64
import com.topjohnwu.superuser.Shell

object ConfigRepository {
    const val MODULE_DIR = "/data/adb/modules/genshin_fps_unlock"
    const val CONFIG_PATH = "$MODULE_DIR/config.json"
    const val STATUS_PATH = "$MODULE_DIR/status.json"
    const val PROP_PATH = "$MODULE_DIR/module.prop"

    val knownGames = listOf(
        GamePackage("com.miHoYo.Yuanshen", "Yuan Shen (CN)"),
        GamePackage("com.miHoYo.GenshinImpact", "Genshin Impact (OS)"),
        GamePackage("com.miHoYo.ys", "Yuan Shen (Bilibili)"),
    )

    fun ensureShell(): Boolean {
        val shell = Shell.getShell()
        return shell.isRoot
    }

    fun modulePresence(): ModulePresence {
        if (!ensureShell()) {
            return ModulePresence.NoRoot
        }
        val r = Shell.cmd("[ -f $PROP_PATH ] && echo yes || echo no").exec()
        return if (r.out.firstOrNull() == "yes") ModulePresence.Installed else ModulePresence.Missing
    }

    fun readConfig(): UnlockConfig {
        if (!ensureShell()) {
            return UnlockConfig()
        }
        val r = Shell.cmd("cat $CONFIG_PATH").exec()
        if (!r.isSuccess || r.out.isEmpty()) {
            return UnlockConfig()
        }
        return UnlockConfig.fromJson(r.out.joinToString("\n"))
    }

    fun writeConfig(config: UnlockConfig): Boolean {
        if (!ensureShell()) {
            return false
        }
        val json = config.toJson()
        val b64 = Base64.encodeToString(json.toByteArray(Charsets.UTF_8), Base64.NO_WRAP)
        val r = Shell.cmd(
            "mkdir -p $MODULE_DIR",
            "echo $b64 | base64 -d > $CONFIG_PATH",
            "chmod 644 $CONFIG_PATH",
        ).exec()
        return r.isSuccess
    }

    fun readStatus(presence: ModulePresence = modulePresence()): UnlockStatus {
        if (presence != ModulePresence.Installed) {
            return UnlockStatus(presence = presence)
        }
        val r = Shell.cmd("cat $STATUS_PATH").exec()
        if (!r.isSuccess || r.out.isEmpty()) {
            return UnlockStatus(presence = presence, state = "none")
        }
        val raw = r.out.joinToString("\n")
        fun str(key: String) = """"$key"\s*:\s*"([^"]*)"""".toRegex()
            .find(raw)?.groupValues?.get(1).orEmpty()
        fun int(key: String) = """"$key"\s*:\s*(-?\d+)""".toRegex()
            .find(raw)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        return UnlockStatus(
            presence = presence,
            state = str("status").ifEmpty { "none" },
            mode = str("mode").ifEmpty { "unknown" },
            pid = int("pid"),
            fps = int("fps"),
            message = str("message"),
        )
    }
}
