plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.serialization)
}
android {
    namespace = "dev.waystone.data"
    compileSdk = 34
    defaultConfig { minSdk = 26 }
    testOptions {
        unitTests.all { it.systemProperty("jna.library.path", "${rootDir}/../target/debug") }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
dependencies {
    api(project(":engine"))
    api(libs.okhttp)
    api(libs.kotlinx.coroutines.android)
    api(libs.datastore.preferences)
    api(libs.kotlinx.serialization.json)
    implementation(libs.documentfile)
    testImplementation(libs.junit)
    testImplementation(libs.okhttp.mockwebserver)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.jna)
}
