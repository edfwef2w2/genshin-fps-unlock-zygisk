plugins {
    id("com.android.library")
}

android {
    namespace = "io.github.unlockfps.zygisk"
    compileSdk = 34

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

tasks.register<Copy>("copyZygiskSo") {
    group = "distribution"
    dependsOn("mergeReleaseNativeLibs")

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
    )

    from({
        candidates.map { it.get().asFile }.firstOrNull { it.exists() }
            ?: error("Native lib output not found. Install NDK and run :zygisk:assembleRelease.")
    })
    include("libunlockfps.so")
    rename { "arm64-v8a.so" }
    into(rootProject.layout.projectDirectory.dir("magisk/zygisk"))
}
