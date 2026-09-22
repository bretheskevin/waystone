plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}
android {
    namespace = "dev.waystone.engine"
    compileSdk = 34
    ndkVersion = findProperty("waystone.ndkVersion") as String
    defaultConfig { minSdk = 26 }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets["main"].java.srcDir("../../mobile/bindings")
}
kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
val cargoNdkBuild = tasks.register<Exec>("cargoNdkBuild") {
    val jniLibs = layout.projectDirectory.dir("src/main/jniLibs")
    workingDir = rootDir.parentFile
    commandLine("cargo", "ndk", "-t", "arm64-v8a", "-t", "x86_64",
        "-o", jniLibs.asFile.absolutePath,
        "build", "-p", "waystone-mobile", "--release")
    outputs.dir(jniLibs)
}
tasks.matching { it.name == "mergeDebugJniLibFolders" || it.name == "mergeReleaseJniLibFolders" }
    .configureEach { dependsOn(cargoNdkBuild) }
dependencies { api(libs.jna) { artifact { type = "aar" } } }
