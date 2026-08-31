plugins {
    id("com.android.library")
}

android {
    namespace = "io.github.unlockfps.zygisk"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        minSdk = 26
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
                cppFlags += "-std=c++20"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    packaging {
        jniLibs {
            keepDebugSymbols += "**/*.so"
        }
    }
}

tasks.register("copyZygiskSo") {
    group = "distribution"
    dependsOn("assembleRelease")
    doLast {
        val destDir = rootProject.layout.projectDirectory.dir("magisk/zygisk").asFile
        val dest = destDir.resolve("arm64-v8a.so")
        destDir.mkdirs()
        val candidates = listOf(
            layout.buildDirectory.dir(
                "intermediates/merged_native_libs/release/mergeReleaseNativeLibs/out/lib/arm64-v8a"
            ),
            layout.buildDirectory.dir(
                "intermediates/merged_native_libs/release/out/lib/arm64-v8a"
            ),
            layout.buildDirectory.dir(
                "intermediates/library_and_local_jars_jni/release/jni/arm64-v8a"
            ),
            layout.buildDirectory.dir("intermediates/cxx/RelWithDebInfo"),
        )
        val so = candidates
            .map { it.get().asFile }
            .filter { it.exists() }
            .flatMap { dir -> dir.walkTopDown().filter { it.name == "libunlockfps.so" }.toList() }
            .firstOrNull()
            ?: error("Native lib output not found. Build :zygisk:assembleRelease with the NDK.")
        so.copyTo(dest, overwrite = true)
    }
}
