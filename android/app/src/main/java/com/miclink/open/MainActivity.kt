package com.miclink.open

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import com.miclink.open.theme.ThemeState
import com.miclink.open.ui.navigation.AppNav
import com.miclink.open.ui.theme.WomicTheme

/**
 * MainActivity -- WO Mic 采集端 UI（Compose Material 3，抽屉导航）。
 *
 * 布局风格参考 MicYou：常用设置平铺在主页，不套多层设置菜单；外观/关于等
 * 低频入口收到横向抽屉（[AppNav]）。
 *
 * 主题：由 [WomicTheme] 统一提供（动态取色 / 8 套种子调色板 / AMOLED 纯黑），
 * 设置经 [MainViewModel.themeState] 驱动，随状态重组即时生效；
 * 持久化在 ThemePrefs（经 [Prefs] 委托），重启后保持。
 *
 * 与服务的通信：绑定 [MicCaptureService] + [MicCaptureService.CaptureStatusListener]
 * 回调（主线程），状态汇入 [MainViewModel]；统计由 500ms 轮询补齐。
 *
 * 「停止」二次确认（C-05）：主页按钮与前台服务通知的「停止」都只置
 * [MainViewModel.stopConfirmPending]，由 AppNav 的统一 AlertDialog 确认后才
 * 真正 [onStopClicked]——通知侧经 [ACTION_CONFIRM_STOP] Intent 打开本
 * Activity 触发（见 [handleConfirmStopIntent]），两端共用同一条确认路径。
 */
class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "MainActivity"
        private const val UI_REFRESH_INTERVAL_MS = 500L

        /** 通知「停止」动作的 Intent action（打开本 Activity 进入确认流程）。 */
        const val ACTION_CONFIRM_STOP = "com.miclink.open.action.CONFIRM_STOP"

        /** 确认停止的 Intent extra（[handleConfirmStopIntent] 识别用）。 */
        const val EXTRA_CONFIRM_STOP = "extra.confirm_stop"
    }

    private val vm: MainViewModel by viewModels()

    /**
     * 运行时权限申请（Activity Result API）。
     *
     * 回调里**重新检查实际权限状态**而不是信任回调 Map：用户直接划掉对话框时
     * 部分 ROM 会回传空 Map（`all {}` 对空集合恒为 true，会误判为全部授予）。
     */
    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        if (Permissions.allGranted(this)) {
            startCaptureService()
        } else {
            vm.onError(getString(R.string.permission_denied, Permissions.describeMissing(this)))
        }
    }

    private var service: MicCaptureService? = null
    private var bound = false
    private val mainHandler = Handler(Looper.getMainLooper())

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val svc = (binder as? MicCaptureService.LocalBinder)?.getService()
            service = svc
            bound = svc != null
            svc?.setStatusListener(statusListener)
            svc?.let {
                vm.onServiceState(it.currentState(), it.currentDetail())
                vm.onListeningAddress(it.listeningAddress())
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
            bound = false
        }
    }

    private val statusListener = object : MicCaptureService.CaptureStatusListener {
        override fun onState(state: String, detail: String) {
            vm.onServiceState(state, detail)
            service?.let { vm.onListeningAddress(it.listeningAddress()) }
        }

        override fun onLevel(rmsDb: Float, peakDb: Float) {
            vm.onLevel(rmsDb, peakDb)
        }

        override fun onError(message: String) {
            vm.onError(message)
        }

        override fun onMessage(message: String) {
            vm.onMessage(message)
        }
    }

    /** 定时刷新统计与监听地址。 */
    private val uiRefreshRunnable = object : Runnable {
        override fun run() {
            vm.pollStats(service)
            // 采集选项变更即时推送给运行中的服务（500ms 内生效，见
            // MicCaptureService.updateCaptureSettings：推流中会重建 AudioRecord）
            vm.pushCaptureSettings(service)
            // 音量档位 / 静音态即时推送（D-18：运行中可调，见 MicCaptureService.updateVolume）
            vm.pushVolume(service)
            // 传输方式实读（D-16 诚实性：真实网络类型，不写死「Wi-Fi」）
            vm.onTransportChanged(transportLabel(this@MainActivity))
            service?.let { vm.onListeningAddress(it.listeningAddress()) }
            mainHandler.postDelayed(this, UI_REFRESH_INTERVAL_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 启动自检（缺陷 D-11）：默认值必须与 build.gradle.kts 的 BuildConfig 一致、
        // 主题默认值与预置表自洽，否则直接崩在启动阶段，而不是静默丢设置。
        Prefs.verifyDefaults()
        val prefs = Prefs(this)
        vm.loadConfig(prefs)
        vm.loadTheme(prefs)

        // 通知「停止」经本 Activity 打开（singleTop）：冷启动走 onCreate 的
        // intent，热启动走 onNewIntent；均只置确认标志，不直接停服务（C-05）。
        if (savedInstanceState == null) {
            handleConfirmStopIntent(intent)
        }

        setContent {
            val theme: ThemeState = vm.themeState
            WomicTheme(
                mode = theme.mode,
                palette = theme.palette,
                amoled = theme.amoled
            ) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    AppNav(
                        vm = vm,
                        onStartClicked = { onStartClicked() },
                        onStopClicked = { onStopClicked() }
                    )
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        // singleTop 复用：更新 Activity 持有的 intent，并走确认停止分支
        setIntent(intent)
        handleConfirmStopIntent(intent)
    }

    /** 识别通知「停止」Intent：置确认标志（弹窗由 AppNav 统一呈现）。 */
    private fun handleConfirmStopIntent(intent: Intent?) {
        if (intent?.getBooleanExtra(EXTRA_CONFIRM_STOP, false) == true) {
            vm.requestStopConfirmation()
        }
    }

    override fun onStart() {
        super.onStart()
        bindService(
            Intent(this, MicCaptureService::class.java),
            serviceConnection,
            Context.BIND_AUTO_CREATE
        )
    }

    override fun onResume() {
        super.onResume()
        mainHandler.post(uiRefreshRunnable)
    }

    override fun onPause() {
        mainHandler.removeCallbacks(uiRefreshRunnable)
        super.onPause()
    }

    override fun onStop() {
        service?.setStatusListener(null)
        if (bound) {
            try {
                unbindService(serviceConnection)
            } catch (t: Throwable) {
                Log.w(TAG, "unbindService 失败：${t.message}")
            }
            bound = false
        }
        service = null
        super.onStop()
    }

    // 权限结果经 [permissionLauncher]（Activity Result API）回调，无需重写
    // onRequestPermissionsResult（activity 1.13 起该回调已废弃）。

    // ------------------------------------------------------------------
    // 动作
    // ------------------------------------------------------------------

    private fun onStartClicked() {
        val config = vm.readConfig()
        if (config == null) {
            vm.onError(getString(R.string.error_invalid_port))
            return
        }
        Prefs(this).save(config)
        if (!Permissions.allGranted(this)) {
            permissionLauncher.launch(Permissions.missing(this).toTypedArray())
            return
        }
        startCaptureService()
    }

    private fun startCaptureService() {
        val config = vm.readConfig() ?: return
        // 落盘用「静音前档位」（muted 是瞬态不持久化）；START Intent 带
        // 当前生效音量（静音中启动 = 0），避免"界面静音、服务却有声"的窗口。
        val intent = MicCaptureService.buildStartIntent(
            this,
            config.copy(volume = vm.effectiveVolume())
        )
        try {
            ContextCompat.startForegroundService(this, intent)
        } catch (t: Throwable) {
            Log.e(TAG, "启动服务失败", t)
            vm.onError(getString(R.string.error_start_service, t.message ?: ""))
            return
        }
        if (!bound) {
            bindService(
                Intent(this, MicCaptureService::class.java),
                serviceConnection,
                Context.BIND_AUTO_CREATE
            )
        }
    }

    private fun onStopClicked() {
        try {
            startService(MicCaptureService.buildStopIntent(this))
        } catch (t: Throwable) {
            Log.w(TAG, "停止服务失败：${t.message}")
        }
    }
}

/**
 * 当前网络传输方式（主页「传输方式」字段，D-16）。
 *
 * 诚实性：实读 [ConnectivityManager] 的活跃网络能力，不写死「Wi-Fi」；
 * 读不到（无权限/无活跃网络）时返回空串，由 UI 显示「—」。
 */
private fun transportLabel(context: Context): String {
    val unknown = ""
    val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
        ?: return unknown
    return try {
        val network = cm.activeNetwork ?: return unknown
        val caps = cm.getNetworkCapabilities(network) ?: return unknown
        when {
            caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ->
                context.getString(R.string.home_transport_wifi)
            caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) ->
                context.getString(R.string.home_transport_cellular)
            caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) ->
                context.getString(R.string.home_transport_ethernet)
            else -> unknown
        }
    } catch (t: Throwable) {
        Log.w("MainActivity", "读取网络传输方式失败：${t.message}")
        unknown
    }
}
