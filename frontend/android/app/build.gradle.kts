plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.alamshafil.ps5emu"
    compileSdk {
        version = release(37) {
            minorApiLevel = 2
        }
    }

    defaultConfig {
        applicationId = "com.alamshafil.ps5emu"
        minSdk = 29
        targetSdk = 37
        versionCode = 1
        versionName = "0.2.0"

        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    buildFeatures {
        compose = true
    }

    signingConfigs {
        val keystorePath = System.getenv("KYTY_ANDROID_KEYSTORE")
        if (!keystorePath.isNullOrBlank()) {
            create("release") {
                storeFile = file(keystorePath)
                storePassword = System.getenv("KYTY_ANDROID_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("KYTY_ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("KYTY_ANDROID_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Sign with release keystore when configured, otherwise fall back to the debug key
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.directories.addAll(
                listOf(
                    "src/main/java",
                    "../../../3rdparty/SDL3/android-project/app/src/main/java"
                )
            )
            jniLibs.directories.add("src/main/jniLibs")
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    implementation(fileTree(mapOf("dir" to "libs", "include" to listOf("*.aar", "*.jar"))))

    implementation("androidx.core:core-ktx:1.19.1")
    implementation("androidx.appcompat:appcompat:1.8.0")
    implementation("com.google.android.material:material:1.14.0")
    implementation("androidx.constraintlayout:constraintlayout:2.2.2")

    // Jetpack Compose
    val composeBom = platform("androidx.compose:compose-bom:2026.11.01")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material:material")
    implementation("androidx.compose.material:material-icons-core")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.13.0")

    // Skip runtime dependencies
    implementation("org.jetbrains.kotlin:kotlin-reflect:2.4.21")
    implementation("androidx.navigation3:navigation3-runtime:1.2.2")
    implementation("androidx.navigation3:navigation3-ui:1.2.2")
    implementation("androidx.compose.material3.adaptive:adaptive:1.3.0")
    implementation("androidx.compose.material3:material3-adaptive-navigation-suite:1.5.0")
    implementation("androidx.graphics:graphics-path:1.1.0")
    implementation("androidx.work:work-runtime-ktx:2.12.0")
    implementation("io.coil-kt.coil3:coil-compose:3.6.3")
    implementation("com.squareup.okhttp3:okhttp:5.5.1")
    implementation("org.commonmark:commonmark:0.33.0")
    implementation("org.commonmark:commonmark-ext-gfm-strikethrough:0.33.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.13.0")
}
