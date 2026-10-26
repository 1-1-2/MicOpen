package com.miclink.open.ui.navigation

/**
 * 顶部「三段导航」的纯状态机（R-12）——**不依赖 Compose / Android**，因此可以在
 * JVM 单测里直接验证（本地无模拟器，导航行为靠代码级推理 + 单测兜底）。
 *
 * 设计要点：
 *  - 三段 = 连接 / 音源 / 高级，取代原来的抽屉导航（抽屉已整体移除）；
 *  - **禁止横滑切页**：切页动画只是表现（横向滑入 + 淡入），手势必须让位给
 *    音量点轨（与 D-02 同源纪律：页面级横滑不得与点轨抢手势）；
 *  - 切页不重连、不重置滚动：连接状态在 ViewModel / 服务里，与页无关。
 */
enum class HomeTab(val index: Int) {
    /** 连接页：状态卡 + 主按钮 + 推流音量 + 参数摘要。 */
    CONNECT(0),

    /** 音源页：音源六档 chips + 输入设备 chips。 */
    SOURCE(1),

    /** 高级页：系统效果 / 控制端口 / 采样率 / 调试音 / 关于与帮助。 */
    ADVANCED(2)
}

/** 页内锚点：从连接页的「参数摘要」跳过去后，滚到对应分组。 */
enum class HomeFocus {
    /** 无锚点（直接选页）。 */
    NONE,

    /** 音源页：音源 chips（页首）。 */
    SOURCE_CHIPS,

    /** 高级页：系统效果卡。 */
    EFFECTS,

    /** 高级页：控制端口输入框。 */
    PORT
}

/** 顶栏三段从左到右的顺序。 */
val HOME_TAB_ORDER: List<HomeTab> = HomeTab.entries.sortedBy { it.index }

/**
 * 返回键落点（R-12）：
 *  - 在「音源 / 高级」页 → 回「连接」页，**不退出 App**；
 *  - 已在「连接」页 → `null`（**不拦截**，交回系统默认行为 = 退出）。
 */
fun backTargetFrom(tab: HomeTab): HomeTab? =
    if (tab == HomeTab.CONNECT) null else HomeTab.CONNECT

/**
 * R-33 三页常驻布局：页 [page] 相对**连续位置** [pos] 的横向位移（单位 = 页宽）。
 *
 * ⚠ 为什么用「连续位置」而不是「(当前页, 动画进度)」两个量：
 *   旧写法是 `pageOffsetUnits(page, current, slideUnits)`，位移在 `LaunchedEffect`
 *   里才 `snapTo` 成「目标索引 − 起始索引」。而 `LaunchedEffect` 跑在**重组之后**，
 *   于是切页那一帧的绘制读到的是 `current = 新页、slideUnits = 0` —— 新页直接
 *   出现在屏幕中央并被画出去，下一帧才被 snap 到屏外、再滑回来。用户看到的就是
 *   「先闪一下目标卡片，再变成动画」。
 *
 *   改成单一连续位置 [pos] 后，`pos` 是动画状态（重组时仍是**旧值**），而
 *   `current` 已经是新页：位移天然是「新页在屏外、旧页在中央」，动画从这一帧
 *   开始把 pos 送向新索引。**不需要任何 snapTo，也不存在时序差。**
 *
 * 动画只改绘制（`graphicsLayer.translationX`），不改布局尺寸，也不产生任何手势。
 *
 * @param pos 连续位置（动画中的插值结果，如 0.4f），不必是整数
 * @return 0 = 居中；+1 = 右侧一页；−1 = 左侧一页
 */
fun pageOffsetUnits(page: HomeTab, pos: Float): Float = page.index - pos

/**
 * 页 [page] 的淡入淡出 alpha：越靠近屏幕中央越不透明。
 *
 * 退出去的页随着位移离开而自然淡出，**不需要**为"退出页在动画期间仍可见"单独
 * 留特判（旧实现的 `animating && page.index == outgoingIndex` 两个状态因此删除）。
 */
fun pageAlpha(page: HomeTab, pos: Float): Float =
    (1f - kotlin.math.abs(pageOffsetUnits(page, pos))).coerceIn(0f, 1f)

/**
 * R-33：只有当前页可交互（其余页在常驻组合里但必须"看得见地不存在"——
 * 触摸被吞掉 + 无障碍 invisibleToUser）。
 */
fun isPageInteractive(page: HomeTab, current: HomeTab): Boolean = page == current
