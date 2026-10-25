package com.miclink.open

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import com.miclink.open.theme.AppPalette
import com.miclink.open.theme.ThemeMode
import com.miclink.open.theme.ThemeState

/**
 * 主界面状态：配置 + 服务状态 + 会话统计。
 *
 * 全部由 [MainActivity] 从服务回调 / SharedPreferences 灌入；
 * Compose 侧只读 [uiState]，不直接碰服务（关注点分离，旋转屏后状态不丢）。
 *
 * 数据诚实性（定稿规范 D-16）：延迟 / 丢包 app 不测量，UI 恒显示「—」，
 * 本状态不提供任何遥测字段；时长取自服务记录的回话开始时刻（真实值）。
 */
data class MainUiState(
    // 可编辑配置（启动服务时锁定）
    val controlPortText: String = Prefs.DEFAULT_CONTROL_PORT.toString(),
    /**
     * 上一次输入被 5 位上限截掉的位数（0 = 未截断）。
     *
     * QA 缺陷 D-04：粘贴 30 位旧实现只静默留前 5 位、无任何提示。这里把"被忽略了
     * 几位"带到 UI，输入框下方明示（校验链路不变：数字过滤 → take(5) → 1..65535
     * → 启动时明确拒绝）。
     */
    val controlPortDropped: Int = 0,
    val volume: Int = Prefs.DEFAULT_VOLUME,
    val debugTone: Boolean = false,
    /** 静音开关（瞬态 UI 状态，不落盘；协议层以音量 0 实现真静音）。 */
    val muted: Boolean = false,
    // 采集选项（音源 / 麦克风 / 系统效果）：与端口不同，录音中也可改，即时生效
    val capture: CaptureSettings = CaptureSettings(),
    // 当前实际生效的输入设备（AudioRecord.getRoutedDevice；未录音为 null）
    val routedDeviceLabel: String? = null,
    // 服务状态
    val state: String = MicCaptureService.STATE_IDLE,
    val detail: String = "",
    val listeningAddress: String = "",
    /** 本回推流会话开始时刻（SystemClock.elapsedRealtime；0 = 无会话，时长显示「—」）。 */
    val sessionStartElapsedMs: Long = 0L,
    /** 网络传输方式（ConnectivityManager 实读；空 = 未知，UI 显示「—」）。 */
    val transport: String = "",
    // 统计（轮询）
    val packets: Long = 0L,
    val bytes: Long = 0L,
    val failures: Int = 0,
    // 电平（保留服务回调链路；定稿主页不渲染电平表，见 onLevel 注释）
    val rmsDb: Float = LevelMeter.SILENCE_DB,
    val peakDb: Float = LevelMeter.SILENCE_DB,
    // 错误提示（一次性事件，UI 展示后应调 [consumeMessage]）
    val error: String? = null,
    // 非致命提示（一次性事件，服务仍在运行；与 error 共用同一弹窗，error 优先）
    val message: String? = null,
    // 「停止」二次确认待决（C-05：主页按钮与通知停止共用同一确认路径）
    val stopConfirmPending: Boolean = false
) {
    val isRunning: Boolean get() = MicCaptureService.isRunning
    val isStreaming: Boolean get() = MicCaptureService.isStreaming
}

/**
 * MainViewModel -- 主界面的唯一状态容器。
 *
 * 不持有 Service 引用（避免泄漏）：Activity 在 onStart 绑定服务后，
 * 通过 [onServiceState] / [onLevel] / [pollStats] 把状态推进来。
 * 用 Compose [mutableStateOf] 而非 StateFlow：单屏应用无需引入协程依赖。
 */
class MainViewModel : ViewModel() {

    var uiState: MainUiState by mutableStateOf(MainUiState())
        private set

    /**
     * 主题设置（与运行参数分离）：
     * 运行参数是"启动服务前锁定"的编辑态，主题是"改完立刻生效并落盘"的偏好态，
     * 两者生命周期不同，混在一个 state 里会让"什么时候该写盘"变糊。
     */
    var themeState: ThemeState by mutableStateOf(ThemeState())
        private set

    /** 主题存储（[loadTheme] 时保留引用；Prefs 持有 applicationContext，无泄漏风险）。 */
    private var themeStore: Prefs? = null

    /**
     * 配置存储（[loadConfig] 时保留引用）：采集选项每次变更即时落盘用。
     * 与 [themeStore] 分开命名：主题与运行配置的写盘时机不同（见类注释）。
     */
    private var configStore: Prefs? = null

    /** 有待推送给运行中服务的采集选项（下次 [pushCaptureSettings] 时同步）。 */
    private var capturePushPending = false

    /** 有待推送给运行中服务的协议音量（主页点轨 / 静音开关变更后置位）。 */
    private var volumePushPending = false

    /** 静音前的音量档位（取消静音时恢复；前值为 0 时恢复 [Prefs.DEFAULT_VOLUME]）。 */
    private var volumeBeforeMute: Int = Prefs.DEFAULT_VOLUME

    /** Activity 创建时从 Prefs 灌入可编辑配置。 */
    fun loadConfig(prefs: Prefs) {
        configStore = prefs
        val config = prefs.load()
        uiState = uiState.copy(
            controlPortText = config.controlPort.toString(),
            volume = config.volume,
            debugTone = config.debugTone,
            capture = config.capture
        )
    }

    /**
     * Activity 创建时灌入主题设置，并保留存储引用供后续修改落盘。
     *
     * 与 [loadConfig] 的差别：配置只在点"启动服务"时保存，主题每次变更都即时保存
     * （验收 #3：重启保持），所以这里必须留住 [Prefs]（经它委托 ThemePrefs）。
     */
    fun loadTheme(prefs: Prefs) {
        themeStore = prefs
        themeState = prefs.loadTheme()
    }

    /** 把当前 UI 配置读成 [Prefs.Config]（端口文本非法时返回 null）。 */
    fun readConfig(): Prefs.Config? {
        val port = uiState.controlPortText.trim().toIntOrNull() ?: return null
        if (port !in 1..65535) return null
        return Prefs.Config(
            controlPort = port,
            volume = uiState.volume,
            debugTone = uiState.debugTone,
            capture = uiState.capture
        )
    }

    /**
     * 控制端口输入（D-04：超长输入不再"静默"截断）。
     *
     * 校验链路保持原样：数字过滤 → `take(5)`（65535 是最长合法端口）→ 由
     * [readConfig] 做 1..65535 范围校验 → 启动时明确拒绝。唯一的增量是把
     * **被截掉的位数**记进 [MainUiState.controlPortDropped]，由采集设置的高级卡
     * 在输入框下方给出可见提示（用户删到 5 位以内时提示自动消失）。
     */
    fun onControlPortTextChanged(text: String) {
        // 只允许数字，避免非法字符进入配置
        val digits = text.filter { it.isDigit() }
        val filtered = digits.take(5)
        uiState = uiState.copy(
            controlPortText = filtered,
            controlPortDropped = (digits.length - filtered.length).coerceAtLeast(0)
        )
    }

    /**
     * 音量档位变更（主页十档点轨，协议 0-9：0 静音 / 5 原样 / 每档 ±6 dB）。
     *
     * 与旧行为的差别：音量**运行中也可调**（服务 [MicCaptureService.updateVolume]
     * 每包读取），不再"运行中禁用"——那会造成"控件可见可用但无实效果"的假控件，
     * 违反诚实性纪律。变更即时落盘（重启后保持）并推送运行中的服务。
     */
    fun onVolumeChanged(value: Int) {
        val level = value.coerceIn(0, 9)
        if (level == uiState.volume) {
            return
        }
        uiState = uiState.copy(volume = level)
        if (level > 0) {
            volumeBeforeMute = level
        }
        volumePushPending = true
        configStore?.saveVolume(level)
    }

    /**
     * 静音开关（D-18 联动）：
     *  - 开：记住当前档位，协议音量置 0（真静音，推流不中断）；
     *  - 关：恢复静音前档位（前值为 0 时恢复默认档 5）。
     *
     * [muted] 是瞬态 UI 状态（不落盘）：重启后回到已保存的档位。
     */
    fun onMuteChanged(value: Boolean) {
        if (value == uiState.muted) {
            return
        }
        if (value) {
            volumeBeforeMute = uiState.volume
            uiState = uiState.copy(muted = true)
        } else {
            val restore = if (volumeBeforeMute in 1..9) volumeBeforeMute else Prefs.DEFAULT_VOLUME
            uiState = uiState.copy(muted = false, volume = restore)
            configStore?.saveVolume(restore)
        }
        volumePushPending = true
    }

    /** 当前生效的协议音量（静音时 0；START Intent 与推送共用）。 */
    fun effectiveVolume(): Int = if (uiState.muted) 0 else uiState.volume

    fun onDebugToneChanged(value: Boolean) {
        uiState = uiState.copy(debugTone = value)
    }

    // ------------------------------------------------------------------
    // 采集选项（变更即时生效 + 即时落盘，录音中也可改）
    // ------------------------------------------------------------------

    /** 音源模式变更（id 见 [AudioSourceIds]；UI 已对不支持的档位禁用）。 */
    fun onAudioSourceChanged(id: Int) {
        updateCapture(uiState.capture.copy(audioSource = id))
    }

    /** 输入设备变更（[CaptureSettings.MIC_DEVICE_AUTO] = 跟随系统）。 */
    fun onMicDeviceChanged(id: Int) {
        updateCapture(uiState.capture.copy(micDeviceId = id))
    }

    /** 系统降噪（NoiseSuppressor）开关。 */
    fun onNoiseSuppressorChanged(value: Boolean) {
        updateCapture(uiState.capture.copy(noiseSuppressor = value))
    }

    /** 系统回声消除（AcousticEchoCanceler）开关。 */
    fun onEchoCancelerChanged(value: Boolean) {
        updateCapture(uiState.capture.copy(echoCanceler = value))
    }

    /** 系统自动增益（AutomaticGainControl）开关。 */
    fun onGainControlChanged(value: Boolean) {
        updateCapture(uiState.capture.copy(gainControl = value))
    }

    /**
     * 统一的采集选项变更入口：改状态（重组 UI）-> 落盘（重启后保持）-> 标记待推送
     * （服务在推流时由 [pushCaptureSettings] 即时同步过去，重建 AudioRecord）。
     */
    private fun updateCapture(capture: CaptureSettings) {
        if (capture == uiState.capture) {
            return
        }
        uiState = uiState.copy(capture = capture)
        configStore?.saveCaptureSettings(capture)
        capturePushPending = true
    }

    /**
     * 把待推送的采集选项同步给运行中的服务（Activity 的 500ms 刷新循环调用）。
     * 服务未绑定/未启动时跳过：选项已落盘，下次 START 经 Intent 带入。
     */
    fun pushCaptureSettings(service: MicCaptureService?) {
        if (!capturePushPending) return
        val svc = service ?: return
        svc.updateCaptureSettings(uiState.capture)
        capturePushPending = false
    }

    /**
     * 把当前生效的协议音量同步给运行中的服务（Activity 的 500ms 刷新循环调用）。
     *
     * 静音是持续性 UI 状态：静音期间每次刷新都推 0（幂等写，保证"重启服务后
     * 界面仍显示静音"与"服务真的在静音"一致）；档位变更后推一次即可。
     */
    fun pushVolume(service: MicCaptureService?) {
        val svc = service ?: return
        if (!volumePushPending && !uiState.muted) return
        svc.updateVolume(effectiveVolume())
        volumePushPending = false
    }

    // ------------------------------------------------------------------
    // 主题（变更即时生效 + 即时落盘）
    // ------------------------------------------------------------------

    fun onThemeModeChanged(mode: ThemeMode) {
        updateTheme(themeState.copy(mode = mode))
    }

    fun onThemePaletteChanged(palette: AppPalette) {
        updateTheme(themeState.copy(palette = palette))
    }

    fun onThemeAmoledChanged(value: Boolean) {
        updateTheme(themeState.copy(amoled = value))
    }

    /**
     * 统一的主题变更入口：先改状态（触发 WomicTheme 重组，即时生效无闪烁），
     * 再落盘（重启后保持）。两条验收标准（#1/#3）都靠这个方法一次满足。
     */
    private fun updateTheme(state: ThemeState) {
        themeState = state
        themeStore?.saveTheme(state)
    }

    /**
     * 服务状态回调（主线程）。
     *
     * 会话时长诚实性：非推流态一律清零（服务记录的 [sessionStartElapsedMs]
     * 才是唯一事实来源，这里只兜"服务已断开但绑定还没断"的窗口）。
     */
    fun onServiceState(state: String, detail: String) {
        uiState = uiState.copy(
            state = state,
            detail = detail,
            sessionStartElapsedMs =
                if (state == MicCaptureService.STATE_STREAMING) {
                    uiState.sessionStartElapsedMs
                } else {
                    0L
                }
        )
    }

    /** 服务监听地址变化（启动成功后刷新）。 */
    fun onListeningAddress(address: String) {
        uiState = uiState.copy(listeningAddress = address)
    }

    /** 网络传输方式变化（Activity 刷新循环经 ConnectivityManager 实读后灌入）。 */
    fun onTransportChanged(label: String) {
        if (label == uiState.transport) {
            return
        }
        uiState = uiState.copy(transport = label)
    }

    /**
     * 电平回调（主线程，约 20 次/秒）。
     *
     * 保留说明：定稿主页不渲染电平表（D-16 字段行不含电平），但服务->UI 的
     * 电平回调链路**保持不动**——删它会改变服务侧既有行为（遥测埋点依赖它），
     * 且 uiState 上的 rmsDb/peakDb 仍可供后续功能使用，不是死代码。
     */
    fun onLevel(rmsDb: Float, peakDb: Float) {
        uiState = uiState.copy(rmsDb = rmsDb, peakDb = peakDb)
    }

    /** 轮询统计（Activity 的 500ms 刷新循环调用）。 */
    fun pollStats(service: MicCaptureService?) {
        val svc = service
        if (svc == null) {
            // 服务断开（停止/崩溃）：清会话时长，不显示上一会话的残留值
            if (uiState.sessionStartElapsedMs != 0L) {
                uiState = uiState.copy(sessionStartElapsedMs = 0L)
            }
            return
        }
        uiState = uiState.copy(
            packets = svc.packetsSent(),
            bytes = svc.bytesSent(),
            failures = svc.sendFailures(),
            routedDeviceLabel = svc.routedDeviceLabel(),
            // 会话开始时刻是服务级全局状态（单连接服务，同 isStreaming 风格）
            sessionStartElapsedMs = MicCaptureService.sessionStartElapsedMs
        )
    }

    /**
     * 「停止」二次确认待决（C-05）：主页按钮与前台服务通知的「停止」
     * 共用本标志 + AppNav 的统一 AlertDialog，不各有实现。
     */
    fun requestStopConfirmation() {
        uiState = uiState.copy(stopConfirmPending = true)
    }

    /** 确认弹窗关闭（取消 / 已确认并执行停止）。 */
    fun consumeStopConfirmation() {
        uiState = uiState.copy(stopConfirmPending = false)
    }

    /** 错误（服务会自行停止；UI 弹一次性对话框，见 [consumeMessage]）。 */
    fun onError(message: String) {
        uiState = uiState.copy(error = message)
    }

    /** 非致命提示（服务继续运行时的明确告知，如设备切换失败）。 */
    fun onMessage(message: String) {
        uiState = uiState.copy(message = message)
    }

    /** 消费一次性提示（错误优先展示；两者一起清掉）。 */
    fun consumeMessage() {
        uiState = uiState.copy(error = null, message = null)
    }
}
