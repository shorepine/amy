plugins {
    id("com.android.application")
}

android {
    namespace = "org.amy.hello"
    compileSdk = 36

    defaultConfig {
        applicationId = "org.amy.hello"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
    }
}

dependencies {
    // Package the independent :amy Android service in the APK. MainActivity
    // has no Java/JNI dependency on AmyService or on AMY itself; it only uses
    // the app-private amy.sock wire transport.
    implementation(project(":amy-service"))
}
