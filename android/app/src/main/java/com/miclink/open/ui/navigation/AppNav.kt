package com.miclink.open.ui.navigation

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Checkroom
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.miclink.open.MainViewModel
import com.miclink.open.R
import com.miclink.open.ui.screen.AboutScreen
import com.miclink.open.ui.screen.AdvancedPage
import com.miclink.open.ui.screen.AppearanceScreen
import com.miclink.open.ui.screen.ConnectPage
import com.miclink.open.ui.screen.SourcePage
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** 三段外壳之外的两个独立页（外观 / 关于），返回键由导航栈自然回退。 */
private const val ROUTE_HOME: String = "home"
private const val ROUTE_APPEARANCE: String = "appearance"
private const val ROUTE_ABOUT: String = "about"

/** R-05：设置类提示的停留时长（1.8s 自动消失，不用对话框打断）。 */
private const val SNACKBAR_DURATION_MS: Long = 1800L

/**
 * AppNav -- 顶部三段导航（R-12）：`连接 / 音源 / 高级` + 右上角外观（👕）。
 *
 * 与旧实现的差别：
 *  - **抽屉整体移除**（`ModalNavigationDrawer` 不再存在）：三段就在顶栏上，
 *    一屏可达，不需要先拉开抽屉；
 *  - **禁止横滑切页**：切页只由点按三段触发，动画（横向滑入 + 淡入）纯粹是
 *    表现层——页面级横滑手势必须让位给音量点轨（与 D-02 同源纪律）；
 *  - 返回键：在「音源 / 高级」页回「连接」页，**不退出 App**；已在「连接」页
 *    则不拦截（交系统退出）；
 *  - 切页**不重连、不重置滚动**：连接状态在服务 / ViewModel 里，三页的
 *    [ScrollState] 提到本层（`rememberSaveable`），离开再回来位置还在；
 *  - 「停止」二次确认（C-05）与错误弹窗挂在最外层，覆盖任何目的地。
 */
@Composable
fun AppNav(
    vm: MainViewModel,
    onStartClicked: () -> Unit,
    onStopClicked: () -> Unit
) {
    val navController = rememberNavController()

    NavHost(
        navController = navController,
        startDestination = ROUTE_HOME,
        modifier = Modifier.fillMaxSize()
    ) {
        composable(ROUTE_HOME) {
            HomeShell(
                vm = vm,
                onStartClicked = onStartClicked,
                onOpenAppearance = { navController.navigate(ROUTE_APPEARANCE) },
                onOpenAbout = { navController.navigate(ROUTE_ABOUT) }
            )
        }
        composable(ROUTE_APPEARANCE) {
            AppearanceScreen(vm = vm)
        }
        composable(ROUTE_ABOUT) {
            AboutScreen()
        }
    }

    // 停止二次确认（C-05 / D-15）：主页按钮与通知「停止」共用
    StopConfirmDialog(vm = vm, onConfirm = onStopClicked)
    // 错误仍用对话框：它是"服务已停止"这类必须被看到的事实，不是设置提示
    ErrorDialog(vm = vm)
}

/** 三段外壳：顶栏 + 内容区（含返回键与 Snackbar）。 */
@Composable
private fun HomeShell(
    vm: MainViewModel,
    onStartClicked: () -> Unit,
    onOpenAppearance: () -> Unit,
    onOpenAbout: () -> Unit
) {
    val tab = vm.homeTab
    val focus = vm.homeFocus
    val snackbarHostState = remember { SnackbarHostState() }

    // 返回键（R-12）：非「连接」页 → 回「连接」页，不退出 App
    BackHandler(enabled = tab != HomeTab.CONNECT) {
        vm.navigateBackFromTab()
    }

    // R-05：设置类提示（音源 / 设备 / 效果变更）走 Snackbar，~1.8s 自动消失，
    // 不再用对话框打断操作。
    val message = vm.uiState.message
    LaunchedEffect(message) {
        if (message == null) {
            return@LaunchedEffect
        }
        val autoDismiss = launch {
            delay(SNACKBAR_DURATION_MS)
            snackbarHostState.currentSnackbarData?.dismiss()
        }
        snackbarHostState.showSnackbar(
            message = message,
            // 停留时长由上面的定时器控制（M3 的 Short 是 4s，偏长），故用
            // Indefinite + 主动 dismiss 拿到"1~2 秒自动消失"的手感
            duration = SnackbarDuration.Indefinite
        )
        autoDismiss.cancel()
        vm.consumeMessage()
    }

    // 三页各自的滚动状态提在本层（AnimatedContent 之上）：切页不重置滚动（R-12）。
    // rememberSaveable 保证旋转屏与"去外观页再回来"之后位置仍在。
    val connectScroll: ScrollState = rememberSaveable(saver = ScrollState.Saver) { ScrollState(0) }
    val sourceScroll: ScrollState = rememberSaveable(saver = ScrollState.Saver) { ScrollState(0) }
    val advancedScroll: ScrollState = rememberSaveable(saver = ScrollState.Saver) { ScrollState(0) }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            // R-12 顶栏：**不用 M3 TopAppBar**。
            //
            // 根因（两轮 CI 五屏取证 + dump 实证）：三段自绘分段控件放在
            // TopAppBar 的 title 槽（SubcomposeLayout + 槽位约束）里时，点按
            // 命中与 dump 的 a11y bounds 不一致——三段中只有最后一段能切页，
            // 「连接 / 音源」的 onClick 根本不触发（dump 显示页面纹丝不动、
            // 无崩溃、无异常）；而**同一顶栏 actions 槽的 M3 IconButton**
            // （Surface(onClick) + 48dp 最小触摸目标）两轮全部正常。
            // 修复：顶栏改为普通 Row（无子组合、无槽位约束），三段换用与
            // IconButton 同一家族的 M3 SegmentedButton——其触摸路径就是
            // 实证可用的那条。
            Surface(tonalElevation = 2.dp) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .windowInsetsPadding(WindowInsets.statusBars)
                        .height(64.dp)
                        .padding(horizontal = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    HomeTabRow(tab = tab, onSelect = vm::selectTab, modifier = Modifier.weight(1f))
                    // 外观 / 主题入口：小衣服图标，与三段同高、垂直居中
                    IconButton(onClick = onOpenAppearance) {
                        Icon(
                            imageVector = Icons.Outlined.Checkroom,
                            contentDescription = stringResource(R.string.nav_appearance)
                        )
                    }
                }
            }
        }
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize()
        ) {
            AnimatedContent(
                targetState = tab,
                transitionSpec = {
                    // 横向滑入 + 淡入：只是表现，**没有任何横滑手势**
                    val toRight = slideEnterFromRight(initialState, targetState)
                    val enterOffset: (Int) -> Int =
                        { full: Int -> if (toRight) full else -full }
                    val exitOffset: (Int) -> Int =
                        { full: Int -> if (toRight) -full else full }
                    (slideInHorizontally(initialOffsetX = enterOffset) + fadeIn())
                        .togetherWith(slideOutHorizontally(targetOffsetX = exitOffset) + fadeOut())
                },
                contentKey = { it },
                label = "home-tab-content",
                modifier = Modifier.fillMaxSize()
            ) { current: HomeTab ->
                when (current) {
                    HomeTab.CONNECT -> ConnectPage(
                        vm = vm,
                        scrollState = connectScroll,
                        onStartClicked = onStartClicked,
                        onJump = { row ->
                            val (target, targetFocus) = summaryTarget(row)
                            vm.goToTab(target, targetFocus)
                        }
                    )
                    HomeTab.SOURCE -> SourcePage(
                        vm = vm,
                        scrollState = sourceScroll,
                        focus = focus
                    )
                    HomeTab.ADVANCED -> AdvancedPage(
                        vm = vm,
                        scrollState = advancedScroll,
                        focus = focus,
                        onOpenAbout = onOpenAbout
                    )
                }
            }
        }
    }
}

/**
 * 顶栏三段（R-12）：连接 / 音源 / 高级。
 *
 * 用 M3 [SegmentedButton]（`Surface(onClick)` + 48dp 最小触摸目标，与 IconButton
 * 同一家族）而不是裸 `Modifier.clickable`——CI 实证后者在顶栏里点按命中不稳定
 * （三段中只有最后一段能触发，见 [HomeShell] 顶栏注释）。每段等宽（weight），
 * 高 40dp，与右侧 👕 同高垂直居中；每段带 [key] 避免重组期节点复用歧义。
 */
@Composable
private fun HomeTabRow(tab: HomeTab, onSelect: (HomeTab) -> Unit, modifier: Modifier = Modifier) {
    SingleChoiceSegmentedButtonRow(modifier = modifier) {
        HOME_TAB_ORDER.forEachIndexed { index, item: HomeTab ->
            key(item) {
                SegmentedButton(
                    selected = item == tab,
                    onClick = { onSelect(item) },
                    shape = SegmentedButtonDefaults.itemShape(
                        index = index,
                        count = HOME_TAB_ORDER.size
                    ),
                    modifier = Modifier.weight(1f),
                    label = {
                        Text(
                            text = stringResource(tabLabelRes(item)),
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.Medium,
                            maxLines = 1
                        )
                    }
                )
            }
        }
    }
}

/** 三段页 -> 标题文案。 */
private fun tabLabelRes(tab: HomeTab): Int = when (tab) {
    HomeTab.CONNECT -> R.string.tab_connect
    HomeTab.SOURCE -> R.string.tab_source
    HomeTab.ADVANCED -> R.string.tab_advanced
}

/**
 * 「停止」二次确认弹窗（C-05 / D-15）：覆盖在当前目的地之上。
 *
 * 主页按钮与前台服务通知的「停止」共用本弹窗（同走
 * [MainViewModel.stopConfirmPending]）——单次误操作不应当不可逆。
 */
@Composable
private fun StopConfirmDialog(vm: MainViewModel, onConfirm: () -> Unit) {
    val state = vm.uiState
    if (!state.stopConfirmPending) {
        return
    }
    AlertDialog(
        onDismissRequest = { vm.consumeStopConfirmation() },
        title = { Text(stringResource(R.string.home_stop_confirm_title)) },
        text = { Text(stringResource(R.string.home_stop_confirm_text)) },
        confirmButton = {
            TextButton(
                onClick = {
                    vm.consumeStopConfirmation()
                    onConfirm()
                }
            ) {
                Text(
                    text = stringResource(R.string.home_stop_confirm_ok),
                    color = MaterialTheme.colorScheme.error
                )
            }
        },
        dismissButton = {
            TextButton(onClick = { vm.consumeStopConfirmation() }) {
                Text(stringResource(R.string.home_stop_confirm_cancel))
            }
        }
    )
}

/**
 * 错误弹窗（服务停止类）。
 *
 * 区分对待：错误（服务已停）必须被明确看到 → 对话框；
 * 设置变更类提示 → Snackbar（R-05，不打断）。
 */
@Composable
private fun ErrorDialog(vm: MainViewModel) {
    val error = vm.uiState.error
    if (error == null) {
        return
    }
    AlertDialog(
        onDismissRequest = { vm.consumeMessage() },
        title = { Text(stringResource(R.string.app_name)) },
        text = { Text(error) },
        confirmButton = {
            TextButton(onClick = { vm.consumeMessage() }) {
                Text("知道了")
            }
        }
    )
}
