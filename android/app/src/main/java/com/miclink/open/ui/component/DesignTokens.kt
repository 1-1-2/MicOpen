package com.miclink.open.ui.component

import androidx.compose.runtime.ProvidableCompositionLocal
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp

/**
 * MicLink 设计令牌（产品设计手册 §4.1–§4.3 + 定稿规范 §0）。
 *
 * 唯一事实来源：手册令牌表与两条混合公式（徽标 12% / pill 8%）。**禁止**在本文件
 * 之外手调新颜色；需要新色相时先改手册，再改这里。
 *
 * 例外（手册 §4.1 规则 5 / §6.2 明文许可）：[accent] 是唯一可被 Android 动态取色 /
 * 种子调色板替换的语义色，因此它由调用方（WomicTheme）从当前 ColorScheme.primary
 * 注入；[onAccent] 同理。`ok / warn / err / text-* / bg-* / border` 两端固定，
 * 不参与动态取色（否则「红=错误」会失效）。
 *
 * 派生色不落常量：徽标底 = 语义色 12% 混合 bg-card（手册 §4.1 规则 3），运行时按
 * 公式计算， accent 换色后派生底自动跟随，不会出现「底是蓝的、字是紫的」漂移。
 */
data class MicLinkTokens(
    /** 「可操作 / 当前选中」：primary 底、选中 chip / 档位点、进行中状态。 */
    val accent: Color,
    /** accent 上的文字（白）。 */
    val onAccent: Color,
    /** 已连接 / 就绪 / 健康区间。 */
    val ok: Color,
    /** 临界（延迟 / 丢包 / 电平越界）。 */
    val warn: Color,
    /** 断开 / 未就绪 / 破坏性操作。 */
    val err: Color,
    /** 主文本。 */
    val text1: Color,
    /** 次要文本。 */
    val text2: Color,
    /** 禁用与提示。 */
    val text3: Color,
    /** 卡片底（仅用于 12% 混合公式的分母，卡片背景仍取 ColorScheme.surface 以兼容 AMOLED）。 */
    val bgCard: Color,
    /** 轨道 / 输入框 / 日志底。 */
    val bgSunken: Color,
    /** 1dp 描边与分隔线。 */
    val border: Color
) {
    /** 徽标底 = 语义色 12% 混合 bg-card（手册 §4.1 规则 3；浅色 ok `#E2EFE7` 等值即此公式）。 */
    val okContainer: Color get() = lerp(bgCard, ok, 0.12f)

    /** 同上，warn 12%。 */
    val warnContainer: Color get() = lerp(bgCard, warn, 0.12f)

    /** 同上，err 12%。 */
    val errContainer: Color get() = lerp(bgCard, err, 0.12f)

    /** 同上，accent 12%（accent 动态取色时自动跟随）。 */
    val accentContainer: Color get() = lerp(bgCard, accent, 0.12f)
}

/**
 * 浅色令牌（手册 §4.1 浅色列）。
 *
 * @param accent 主题 primary（默认品牌蓝，即手册令牌表浅色 accent `#2A6DF4`）。
 * @param onAccent accent 上的文字。
 */
fun lightMicLinkTokens(
    accent: Color = Color(0xFF2A6DF4),
    onAccent: Color = Color.White
): MicLinkTokens = MicLinkTokens(
    accent = accent,
    onAccent = onAccent,
    ok = Color(0xFF0E7C3A),
    warn = Color(0xFFC2520C),
    err = Color(0xFFC4262E),
    text1 = Color(0xFF1A1D21),
    text2 = Color(0xFF5A6470),
    text3 = Color(0xFF8A94A0),
    bgCard = Color(0xFFFFFFFF),
    bgSunken = Color(0xFFEEF1F5),
    border = Color(0xFFDDE3EA)
)

/**
 * 深色令牌（手册 §4.1 深色列）。
 *
 * @param accent 主题 primary（默认手册深色 accent `#7EA8FF`）。
 * @param onAccent accent 上的文字。
 */
fun darkMicLinkTokens(
    accent: Color = Color(0xFF7EA8FF),
    onAccent: Color = Color(0xFF0A1930)
): MicLinkTokens = MicLinkTokens(
    accent = accent,
    onAccent = onAccent,
    ok = Color(0xFF54CE7F),
    warn = Color(0xFFFFB067),
    err = Color(0xFFFF7A85),
    text1 = Color(0xFFE8EBEF),
    text2 = Color(0xFFA6B0BC),
    text3 = Color(0xFF6E7883),
    bgCard = Color(0xFF1C2023),
    bgSunken = Color(0xFF101315),
    border = Color(0xFF2C3236)
)

/**
 * 设计令牌的 CompositionLocal 入口。
 *
 * 由 [com.miclink.open.ui.theme.WomicTheme] 按最终明暗提供（默认浅色，供
 * 预览 / 测试在主题外直接取用）。UI 零件一律 `LocalMicLinkTokens.current`
 * 取色，不各自硬编码。
 */
val LocalMicLinkTokens: ProvidableCompositionLocal<MicLinkTokens> =
    staticCompositionLocalOf { lightMicLinkTokens() }
