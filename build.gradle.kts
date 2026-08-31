plugins {
    id("com.android.application") version "8.13.2" apply false
    id("com.android.library") version "8.13.2" apply false
    id("org.jetbrains.kotlin.android") version "2.1.20" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.20" apply false
}

tasks.register<Zip>("packageMagiskZip") {
    group = "distribution"
    description = "Package the Magisk/Zygisk zip (includes companion APK)"
    dependsOn(":zygisk:copyZygiskSo", ":app:assembleRelease")

    val magiskDir = layout.projectDirectory.dir("magisk")
    val outDir = layout.projectDirectory.dir("out")
    val apk = layout.projectDirectory
        .dir("app/build/outputs/apk/release/app-release.apk")

    archiveFileName.set("genshin-fps-unlock-zygisk.zip")
    destinationDirectory.set(outDir)

    from(magiskDir) {
        exclude("zygisk/.gitkeep")
    }
    from(apk) {
        rename { "app.apk" }
    }
    doFirst {
        val so = magiskDir.file("zygisk/arm64-v8a.so").asFile
        require(so.exists()) {
            "Missing ${so.path}. Build :zygisk first (requires Android NDK)."
        }
    }
}
