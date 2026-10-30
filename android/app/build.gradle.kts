plugins {
    id("com.android.application")
    // AGP 9.0 起 Kotlin 支持内置于 AGP（运行时依赖 KGP），无需再显式应用
    // org.jetbrains.kotlin.android（应用它会直接报错）。Compose 编译器插件
    // 仍是独立的 KGP 编译器插件，需显式应用（版本与 Kotlin 对齐）。
    alias(libs.plugins.kotlin.compose)
}

// ABI 过滤：默认三 ABI（arm64-v8a / armeabi-v7a / x86_64）。
// CI 的模拟器联调只需 x86_64，用 -PwomicAbis=x86_64 收窄，省两次跨 ABI
// 原生编译（opus + JNI）；需要全 ABI 时不传该属性即可。
val womicAbis: List<String> = (project.findProperty("womicAbis") as String?)
    ?.split(",")
    ?.map { it.trim() }
    ?.filter { it.isNotEmpty() }
    ?.takeIf { it.isNotEmpty() }
    ?: listOf("arm64-v8a", "armeabi-v7a", "x86_64")

android {
    namespace = "com.miclink.open"
    // Android 17 (API 37)：Compose 1.12 / core-ktx 1.19 / lifecycle 2.11 要求
    compileSdk = 37
    // 与构建组件安装的 platforms;android-37.0 精确对应；不依赖 runner 预装的旧 android-37 别名。
    compileSdkMinor = 0

    defaultConfig {
        applicationId = "com.miclink.open"
        // minSdk 26：AudioRecord.Builder / 前台服务通知渠道 / Opus 均需要较新的 API
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        buildConfigField("String", "SOURCE_REV", "\"${providers.environmentVariable("GITHUB_SHA").orElse("local").get()}\"")

        // ---- WO Mic 协议默认值（唯一事实来源：docs/protocol/womic-wifi-protocol-v1.md）----
        // 手机侧 TCP 控制端口，官方 app SharedPreferences "controlPort"（证据 V1/f.java）
        buildConfigField("int", "DEFAULT_CONTROL_PORT", "8125")
        // UDP 头音量字段，单位增益（证据 V1/f.java + 规格 v1 第 3 节格式 C）
        buildConfigField("int", "DEFAULT_VOLUME", "5")
        // CONFIG 采样率表索引：0/1/2 = 8000/16000/48000，官方默认 48000
        buildConfigField("int", "DEFAULT_SAMPLE_RATE_INDEX", "2")
        // 心跳看门狗周期（证据 V1/k.java）
        buildConfigField("long", "HEARTBEAT_TIMEOUT_MS", "5000")
        // Opus 帧长（证据 V1/m.java：app 硬编码 20ms）
        buildConfigField("int", "DEFAULT_FRAME_MS", "20")
        // 语音在 Wi-Fi 上的稳妥档位
        buildConfigField("int", "DEFAULT_BITRATE", "64000")
    }

    signingConfigs {
        // 发布私钥只从本机环境读取，不放入源码或 CI。CI 的 release 由显式测试签名验证。
        val path = providers.environmentVariable("MICLINK_ANDROID_KEYSTORE").orNull
        if (!path.isNullOrBlank()) {
            create("distribution") {
                storeFile = file(path)
                storePassword = providers.environmentVariable("MICLINK_ANDROID_STORE_PASSWORD").get()
                keyAlias = providers.environmentVariable("MICLINK_ANDROID_KEY_ALIAS").get()
                keyPassword = providers.environmentVariable("MICLINK_ANDROID_KEY_PASSWORD").get()
            }
        }
    }

    buildTypes {
        release {
            // R8：代码压缩（minify）+ **资源压缩**（shrinkResources）。
            // R-47：shrinkResources 之前是关的 —— R8 只裁代码不裁资源，
            // res 里没被引用的图标/布局字节全部随包带出去。
            // 两者都开后 R8 才是完整的"无用物剔除"：dex 裁类、
            // res 裁掉连字符串表里都没人引用的条目；native 符号处理由 AGP/NDK 完成。
            // ⚠ keep 规则见 proguard-rules.pro：JNI 类 / Manifest 声明的四组件
            //   已显式 keep，Compose 在 AGP 9 的默认规则里处理（无反射风险）。
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            isDebuggable = false
            signingConfig = signingConfigs.findByName("distribution")
                ?: if (providers.gradleProperty("miclinkTestSigning").orNull == "true") {
                    signingConfigs.getByName("debug")
                } else null // 无发布密钥时默认生成 unsigned release，不默默使用调试签名。
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    defaultConfig {
        externalNativeBuild {
            cmake {
                // 默认只打主流 ABI；需要全 ABI 时改成空数组或删除 abiFilters。
                // CI 模拟器联调只装 x86_64：./gradlew -PwomicAbis=x86_64
                abiFilters.clear()
                abiFilters += womicAbis
                cFlags += listOf("-fvisibility=hidden", "-O2")
                cppFlags += listOf("-fvisibility=hidden", "-O2", "-std=c++17")
            }
        }
        ndk {
            abiFilters.clear()
            abiFilters += womicAbis
        }
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
            // R-47：AAR 里各依赖自带的 LICENSE 全文（lifecycle/collection/ktx
            // 各一份相同的 10 KB），加起来 ~50 KB 纯文本 —— 与编译产物无关，
            // 许可信息在 README/NOTICE 里已交代，不该每次都随包带走。
            // ⚠ 路径是 META-INF/<group>/<artifact>/LICENSE.txt 两层，单层 * 不够。
            excludes += "META-INF/**/LICENSE.txt"
            excludes += "META-INF/LICENSE*"
            excludes += "META-INF/NOTICE*"
        }
    }

    buildFeatures {
        buildConfig = true
        compose = true
        viewBinding = false
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    // 协议核心（纯 JVM）：帧编解码 / 会话状态机 / UDP 包化 + 字节级单元测试
    implementation(project(":core"))

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    // 抽屉导航 NavHost（T05）：三个目的地 = 主页 / 外观 / 关于与帮助
    implementation(libs.androidx.navigation.compose)

    val composeBom = platform(libs.androidx.compose.bom)
    implementation(composeBom)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    debugImplementation(libs.androidx.compose.ui.tooling)

    // 主题系统的种子配色生成（Material You 的 HCT 色彩空间实现）。
    // 版本 4.0.5 是兼容性选择（风险 R12）：5.0.x 的类文件是 Kotlin 2.4 元数据，
    // 本项目的 Kotlin 编译器 2.2.21 读不了（"incompatible version" 编译失败）；
    // 4.0.5 的元数据是 2.0，且其 kotlin-stdlib 依赖正好是 2.2.21，无版本冲突。
    // 它 POM 里的 org.jetbrains.compose.* 是 Compose Multiplatform 重分发，
    // Android 侧全部由 composeBom 提供，排除掉可避免拉一整套 JB 变体。
    implementation(libs.material.kolor) {
        exclude(group = "org.jetbrains.compose")
    }

    // 主题/配色的纯 JVM 单元测试（AMOLED 梯度、WCAG 对比度、预置表自洽）
    testImplementation(libs.junit)
}
