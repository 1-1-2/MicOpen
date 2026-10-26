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

    /**
     * 「点击即切换」的状态机证明：点击后 [pageOffsetUnits] 在动画起点（slideUnits =
     * 目标索引−起始索引）就已经把**目标页定位到"旧页原位"、旧页留在原位准备退出**，
     * 动画只把这个位移送回 0 —— 位移与状态同步，不存在"等动画才开始"的延迟。
     */
    @Test
    fun tabSwitchIsImmediate_offsetsFollowStateSynchronously() {
        // 点「音源」（0 → 1）：动画起点 slideUnits = +1
        assertEquals("新页在右侧一位（准备滑入）", 1f, pageOffsetUnits(HomeTab.SOURCE, HomeTab.SOURCE, 1f))
        assertEquals("旧页仍在原位", 0f, pageOffsetUnits(HomeTab.CONNECT, HomeTab.SOURCE, 1f))
        // 动画结束（slideUnits = 0）
        assertEquals(0f, pageOffsetUnits(HomeTab.SOURCE, HomeTab.SOURCE, 0f))
        assertEquals(-1f, pageOffsetUnits(HomeTab.CONNECT, HomeTab.SOURCE, 0f))
        // 反向（高级 → 连接，1 → 0 的反向位移）：slideUnits = −1
        assertEquals(-1f, pageOffsetUnits(HomeTab.CONNECT, HomeTab.CONNECT, -1f))
        assertEquals(0f, pageOffsetUnits(HomeTab.SOURCE, HomeTab.CONNECT, -1f))
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

    @Test
    fun slideDirection_followsTabOrder() {
        assertTrue(slideEnterFromRight(HomeTab.CONNECT, HomeTab.SOURCE))
        assertTrue(slideEnterFromRight(HomeTab.SOURCE, HomeTab.ADVANCED))
        assertTrue(slideEnterFromRight(HomeTab.CONNECT, HomeTab.ADVANCED))
        // 反向：从右往左切
        assertTrue(!slideEnterFromRight(HomeTab.ADVANCED, HomeTab.CONNECT))
        assertTrue(!slideEnterFromRight(HomeTab.SOURCE, HomeTab.CONNECT))
    }
}
