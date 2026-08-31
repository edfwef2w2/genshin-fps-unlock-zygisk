# Genshin FPS Unlock Zygisk

Unlock the in-game FPS cap on official Android clients (CN / Global / Bilibili).

This is a **Zygisk module** plus a companion app. It injects into the game process, locates the frame-rate value (IL2CPP pattern, with a Unity `set_targetFrameRate` fallback), and writes the target FPS in a loop.

## Upstream

Android port of the Windows tool **[34736384/genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock)** (MIT).

This repository does **not** contain the original Windows / .NET code. Idea and approach follow that project; the Android implementation is new.

## Requirements

- ARM64 device
- Root: Magisk 24+, KernelSU, or APatch
- A **standalone Zygisk**: [ZygiskNext](https://github.com/Dr-TSNG/ZygiskNext), ReZygisk, or NeoZygisk  
  Built-in Magisk Zygisk is not a support target
- Genshin Impact / 原神 installed

The game often refuses to start on an unhidden root environment. Use whatever hide stack you already trust (ZygiskNext denylist / Shamiko, etc.). This module only injects into the configured package names.

## Install

1. Build, or grab a release:
   - `out/genshin-fps-unlock-zygisk.zip`
   - companion APK (also packed inside the zip as `app.apk`)
2. Flash the zip in Magisk / KernelSU / APatch
3. Reboot
4. Open **Genshin FPS Unlock**, grant root, set FPS, tap **Start Game**
5. Leave the module enabled; the companion app can be closed after the game reports *Unlock ready*

`logcat -s UnlockFPS` shows inject / pattern / fallback logs.

## Usage

- FPS range: 10–240 (default 120)
- **Power save**: drop to 10 FPS while the game is not in the foreground
- Target FPS above the panel refresh rate will still be vsynced down — enable 90/120 Hz in system settings
- Regions: `com.miHoYo.Yuanshen`, `com.miHoYo.GenshinImpact`, `com.miHoYo.ys`

Config file (written by the app, readable by the module):

`/data/adb/modules/genshin_fps_unlock/config.json`

```json
{
  "fps": 120,
  "powerSave": false,
  "delaySeconds": 8,
  "autoStart": false,
  "packages": [
    "com.miHoYo.Yuanshen",
    "com.miHoYo.GenshinImpact",
    "com.miHoYo.ys"
  ]
}
```

## Build

Need Android SDK, NDK, CMake, and JDK 17+ (Android Studio is enough). Gradle 9.1 is required if the daemon JDK is 25.

```bat
gradlew.bat :app:assembleRelease :zygisk:assembleRelease packageMagiskZip
```

Outputs:

- `app/build/outputs/apk/release/app-release.apk`
- `magisk/zygisk/arm64-v8a.so`
- `out/genshin-fps-unlock-zygisk.zip`

Set `sdk.dir` in `local.properties`, or open the project in Android Studio and let it generate that file.

## Notes

- HoYoverse is aware of FPS unlock on PC; using **only** FPS unlock is widely reported as safe there. Android root/Zygisk detection is a different problem and may block launch.
- Extra third-party plugins are at your own risk.
- Stuttering after unlocking is not treated as an unlocker bug.
- The IL2CPP byte pattern can break on a new game version. The Unity icall fallback still tries to set FPS; if the game clamps it back, the pattern path needs an update.

## License

[Apache License 2.0](LICENSE). Attribution for the upstream project is in [NOTICE](NOTICE).

---

# 原神 FPS 解锁（Zygisk）

通过 **Zygisk 注入** 在游戏进程内写入目标帧率，并附带一个配置 App。支持国服 / 国际服 / B 服。

## 源头项目

本仓库是 Windows 工具 **[34736384/genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock)**（MIT）的安卓移植。

**不包含** 原仓库的 Windows / .NET 代码。思路沿用原项目，实现全部重写。

## 环境

- ARM64
- Magisk 24+ / KernelSU / APatch
- 独立 Zygisk：**ZygiskNext** / ReZygisk / NeoZygisk（不以 Magisk 内置 Zygisk 为支持目标）
- 已安装原神

未隐藏 Root 时游戏可能无法启动。隐藏方案请用你现有的（ZygiskNext 排除列表 / Shamiko 等）。本模块只注入配置里的包名。

## 安装

1. 刷入 `genshin-fps-unlock-zygisk.zip`，重启
2. 打开 **Genshin FPS Unlock**，授予 Root，设 FPS，点 **Start Game**
3. 状态变为 Unlock ready 后即可。配置 App 可以关掉，模块要留着

排错：`logcat -s UnlockFPS`

## 编译

需要 Android SDK / NDK / CMake / JDK 17+。若本机只有 JDK 25，请使用仓库自带的 Gradle 9.1 wrapper。

```bat
gradlew.bat :app:assembleRelease :zygisk:assembleRelease packageMagiskZip
```

产物在 `app/build/outputs/apk/release/` 和 `out/genshin-fps-unlock-zygisk.zip`。

## 许可证

[Apache License 2.0](LICENSE)。上游项目致谢见 [NOTICE](NOTICE)。
