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

/** 连接页「参数摘要」的三行。保留到摘要区替换为只读状态后移除。 */
enum class SummaryRow {
    SOURCE,
    NOISE,
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

/** 摘要行 → (目标页, 页内锚点)。 */
fun summaryTarget(row: SummaryRow): Pair<HomeTab, HomeFocus> = when (row) {
    SummaryRow.SOURCE -> HomeTab.SOURCE to HomeFocus.SOURCE_CHIPS
    SummaryRow.NOISE -> HomeTab.ADVANCED to HomeFocus.EFFECTS
    SummaryRow.PORT -> HomeTab.ADVANCED to HomeFocus.PORT
}

/**
 * R-33 三页常驻布局：页 [page] 相对当前页 [current] 的**横向位移**（单位 = 页宽）。
 *
 * `slideUnits` 是切页动画的瞬时进度（1 → 0）：切页那一刻 `slideUnits = 目标索引 −
 * 起始索引`，于是新页正好停在"旧页原来的位置"，随后动画把它送回 0——这就是
 * 「点击瞬间整排开始移动」的实现，动画只改绘制（graphicsLayer.translationX），
 * 不改布局尺寸，也不产生任何手势。
 *
 * @return 0 = 居中（当前页）；+1 = 右侧一页；−1 = 左侧一页。
 */
fun pageOffsetUnits(page: HomeTab, current: HomeTab, slideUnits: Float): Float =
    (page.index - current.index) + slideUnits

/**
 * R-33：只有当前页可交互（其余页在常驻组合里但必须"看得见地不存在"——
 * 触摸被吞掉 + 无障碍 invisibleToUser）。
 */
fun isPageInteractive(page: HomeTab, current: HomeTab): Boolean = page == current

/**
 * 切页动画方向：目标页在右侧时新页**从右滑入**、旧页向左滑出，反之亦然。
 * （动画只影响绘制，不产生任何手势——横滑切页是被禁止的。）
 */
fun slideEnterFromRight(from: HomeTab, to: HomeTab): Boolean = to.index >= from.index
