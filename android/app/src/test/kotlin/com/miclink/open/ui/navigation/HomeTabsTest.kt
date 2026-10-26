package com.miclink.open.ui.navigation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4

/**
 * 三段导航（R-12）状态机的纯 JVM 单元测试（本地无模拟器，导航行为靠这里兜底）。
 *
 * 覆盖：
 *  - 顶栏顺序与索引；
 *  - **返回键**：音源 / 高级 → 连接；连接 → 不拦截（交系统退出）；
 *  - **摘要行跳转**：音源 / 降噪 / 端口 各自的目标页与页内锚点；
 *  - **动画方向**：只影响绘制，且左右方向与目标页位置一致（无手势）。
 */
@RunWith(JUnit4::class)
class HomeTabsTest {

    @Test
    fun tabOrder_isConnectSourceAdvanced() {
        assertEquals(
            listOf(HomeTab.CONNECT, HomeTab.SOURCE, HomeTab.ADVANCED),
            HOME_TAB_ORDER
        )
        assertEquals(0, HomeTab.CONNECT.index)
        assertEquals(1, HomeTab.SOURCE.index)
        assertEquals(2, HomeTab.ADVANCED.index)
    }

    // ------------------------------------------------------------------
    // 返回键（R-12）
    // ------------------------------------------------------------------

    @Test
    fun back_fromSourceOrAdvanced_returnsToConnect() {
        assertEquals(HomeTab.CONNECT, backTargetFrom(HomeTab.SOURCE))
        assertEquals(HomeTab.CONNECT, backTargetFrom(HomeTab.ADVANCED))
    }

    /** 已在「连接」页：返回 null = BackHandler 不启用，交回系统（退出 App）。 */
    @Test
    fun back_onConnect_isNotIntercepted() {
        assertNull(backTargetFrom(HomeTab.CONNECT))
    }

    /** 连续返回不会退出：任意页最多一次回退即到连接页。 */
    @Test
    fun back_isIdempotentAfterOneStep() {
        for (start in HomeTab.entries) {
            val first = backTargetFrom(start)
            if (first == null) {
                assertEquals("连接页不应再被拦截", HomeTab.CONNECT, start)
            } else {
                assertNull("回到连接页后不应继续拦截", backTargetFrom(first))
            }
        }
    }

    // ------------------------------------------------------------------
    // 切页动画方向（纯表现，不产生手势）
    // ------------------------------------------------------------------

    // ------------------------------------------------------------------
    // R-33 三页常驻 + 位移切换（点击即切换 / 只有当前页可交互）
    // ------------------------------------------------------------------

    // ------------------------------------------------------------------
    // R-47：切页首帧不能闪出目标页（闪屏回归）
    // ------------------------------------------------------------------

    /**
     * **闪屏修复的机器证明**（R-47）。
     *
     * 旧闪屏的成因：位移是「(新页, 动画进度)」两个量算出来的，动画进度在
     * LaunchedEffect 里才被 snapTo 成起始值，而 LaunchedEffect 跑在重组**之后**
     * ——于是切页那一帧读到「新页 + 进度 0」，新页直接被画在中央。
     *
     * 现在的位移只由连续位置 pos 决定：重组时 pos 仍是**旧索引**（动画尚未推进），
     * 因此本用例断言的正是「切页首帧」这一刻：
     *   · 新页 offset = ±1（在屏外）+ alpha = 0（完全透明）——不可能闪出来；
     *   · 旧页 offset = 0（仍在中央）+ alpha = 1——画面连续。
     */
    @Test
    fun tabSwitchFirstFrame_targetPageIsOffScreenAndTransparent() {
        // 连接(0) → 音源(1)：切页首帧 pos 仍是 0
        assertEquals("新页在屏外右侧", 1f, pageOffsetUnits(HomeTab.SOURCE, 0f))
        assertEquals("新页首帧完全透明（不会闪出来）", 0f, pageAlpha(HomeTab.SOURCE, 0f))
        assertEquals("旧页仍在中央", 0f, pageOffsetUnits(HomeTab.CONNECT, 0f))
        assertEquals("旧页首帧完全不透明（画面连续）", 1f, pageAlpha(HomeTab.CONNECT, 0f))

        // 反向：音源(1) → 连接(0)，切页首帧 pos 仍是 1
        assertEquals("新页在屏外左侧", -1f, pageOffsetUnits(HomeTab.CONNECT, 1f))
        assertEquals(0f, pageAlpha(HomeTab.CONNECT, 1f))
        assertEquals(0f, pageOffsetUnits(HomeTab.SOURCE, 1f))
        assertEquals(1f, pageAlpha(HomeTab.SOURCE, 1f))
    }

    /** 动画终点：新页居中完全不透明，旧页滑出且透明。 */
    @Test
    fun tabSwitchLastFrame_targetPageIsCentered() {
        // 连接(0) → 音源(1) 结束：pos = 1
        assertEquals("新页居中", 0f, pageOffsetUnits(HomeTab.SOURCE, 1f))
        assertEquals("新页完全不透明", 1f, pageAlpha(HomeTab.SOURCE, 1f))
        assertEquals("旧页滑到左侧", -1f, pageOffsetUnits(HomeTab.CONNECT, 1f))
        assertEquals("旧页完全透明", 0f, pageAlpha(HomeTab.CONNECT, 1f))
    }

    /** 位移与 alpha 必须在动画中途连续（不是两态硬切）。 */
    @Test
    fun tabSwitchMidFrame_interpolatesContinuously() {
        // 连接(0) → 音源(1) 走到一半：pos = 0.5
        assertEquals(0.5f, pageOffsetUnits(HomeTab.SOURCE, 0.5f))
        assertEquals(-0.5f, pageOffsetUnits(HomeTab.CONNECT, 0.5f))
        // 两页各半透明，互不遮挡地交叉淡化
        assertEquals(0.5f, pageAlpha(HomeTab.SOURCE, 0.5f))
        assertEquals(0.5f, pageAlpha(HomeTab.CONNECT, 0.5f))
    }

    /** 任意时刻，屏外的页都不该可见（alpha 被夹在 0..1）。 */
    @Test
    fun pageAlpha_isClampedForDistantPages() {
        for (pos in listOf(-1f, 0f, 1f, 2f, 3f)) {
            HomeTab.entries.forEach { page ->
                val a = pageAlpha(page, pos)
                assertTrue("alpha 必须在 0..1（page=$page pos=$pos）", a >= 0f && a <= 1f)
            }
        }
        assertEquals("隔两页的页完全透明", 0f, pageAlpha(HomeTab.ADVANCED, 0f))
    }

    /** 只有当前页可交互：其余两页即使常驻组合也不接受触摸 / 无障碍。 */
    @Test
    fun onlyCurrentTabIsInteractive() {
        assertTrue(isPageInteractive(HomeTab.SOURCE, HomeTab.SOURCE))
        assertTrue(!isPageInteractive(HomeTab.CONNECT, HomeTab.SOURCE))
        assertTrue(!isPageInteractive(HomeTab.ADVANCED, HomeTab.SOURCE))
        // 每一页都恰好一页可交互
        HomeTab.entries.forEach { current ->
            assertEquals(1, HomeTab.entries.count { isPageInteractive(it, current) })
        }
    }

    /**
     * 切页方向（R-47）：方向不再是独立的布尔量，而是 pos 的**单调方向**——
     * 往右切 pos 递增，往左切 pos 递减，页面整排随之移动。
     */
    @Test
    fun slideDirection_isEncodedInPosMonotonicity() {
        // 连接(0) → 高级(2)：pos 从 0 递增到 2，两页的位移都在减小
        assertEquals(2f, pageOffsetUnits(HomeTab.ADVANCED, 0f))   // 首帧：在右侧两页外
        assertEquals(0f, pageOffsetUnits(HomeTab.ADVANCED, 2f))   // 末帧：居中
        assertEquals(1f, pageOffsetUnits(HomeTab.SOURCE, 0f))     // 中间页首帧在右侧一页
        assertEquals(-1f, pageOffsetUnits(HomeTab.SOURCE, 2f))    // 中间页末帧被推到左侧
        // 反向：高级(2) → 连接(0)，pos 从 2 递减到 0
        assertEquals(-2f, pageOffsetUnits(HomeTab.CONNECT, 2f))
        assertEquals(0f, pageOffsetUnits(HomeTab.CONNECT, 0f))
    }
}
