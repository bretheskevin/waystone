plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}
val waystoneVersion: String = rootDir.resolve("../VERSION").readText().trim()
val waystoneVersionCode: Int = waystoneVersion.split('.').let { parts ->
    require(parts.size == 3 && parts.all { p -> p.isNotEmpty() && p.all(Char::isDigit) }) {
        "VERSION must be X.Y.Z, got '$waystoneVersion'"
    }
    val (major, minor, patch) = parts.map(String::toInt)
    require(minor < 100 && patch < 100) { "VERSION minor/patch must be < 100 (versionCode = maj*10000+min*100+patch)" }
    major * 10000 + minor * 100 + patch
}
val releaseKeystore: String? = System.getenv("WAYSTONE_KEYSTORE_PATH")?.takeIf { it.isNotBlank() }
logger.lifecycle(
    "[android] versionName=$waystoneVersion versionCode=$waystoneVersionCode release signing=" +
        if (releaseKeystore != null) "keystore from WAYSTONE_KEYSTORE_PATH" else "none (unsigned release; set WAYSTONE_KEYSTORE_PATH)",
)

android {
    namespace = "dev.waystone.app"
    compileSdk = 34
    defaultConfig {
        applicationId = "dev.waystone"
        minSdk = 26
        targetSdk = 34
        versionCode = waystoneVersionCode
        versionName = waystoneVersion
    }
    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = file(releaseKeystore)
                storePassword = System.getenv("WAYSTONE_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("WAYSTONE_KEY_ALIAS")
                keyPassword = System.getenv("WAYSTONE_KEY_PASSWORD")
            }
        }
    }
    buildTypes {
        release {
            // JNA/UniFFI rely on reflection; R8 is out of scope.
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    testOptions {
        // Same pattern as :data — JVM unit tests load the host waystone_mobile dylib.
        unitTests.all { it.systemProperty("jna.library.path", "${rootDir}/../target/debug") }
    }
}
kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
dependencies {
    implementation(project(":data"))
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.activity.compose)
    implementation(libs.core.ktx)
    implementation(libs.lifecycle.viewmodel.compose)
    implementation(libs.navigation.compose)
    debugImplementation(libs.compose.ui.tooling)
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.jna)
}
