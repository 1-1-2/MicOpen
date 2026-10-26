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
    // 连接页摘要行跳转
    // ------------------------------------------------------------------

    @Test
    fun summaryRow_targets() {
        assertEquals(HomeTab.SOURCE to HomeFocus.SOURCE_CHIPS, summaryTarget(SummaryRow.SOURCE))
        assertEquals(HomeTab.ADVANCED to HomeFocus.EFFECTS, summaryTarget(SummaryRow.NOISE))
        assertEquals(HomeTab.ADVANCED to HomeFocus.PORT, summaryTarget(SummaryRow.PORT))
    }

    /** 三条摘要行各落不同分组（音源行落音源页，另两行落高级页的不同位置）。 */
    @Test
    fun summaryRow_targetsAreDistinct() {
        val targets = SummaryRow.entries.map { summaryTarget(it) }
        assertEquals(targets.size, targets.distinct().size)
        assertEquals(1, targets.count { it.first == HomeTab.SOURCE })
        assertEquals(2, targets.count { it.first == HomeTab.ADVANCED })
    }

    // ------------------------------------------------------------------
    // 切页动画方向（纯表现，不产生手势）
    // ------------------------------------------------------------------

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
