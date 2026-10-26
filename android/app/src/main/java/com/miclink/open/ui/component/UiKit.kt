package com.miclink.open.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.FlowRowScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.ChevronRight
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miclink.open.R

/**
 * 跨端 UI 零件（与 Windows 客户端同一套设计语言：语义色 / 圆角节奏 / 控件形态）。
 *
 * 颜色一律取自 [LocalMicLinkTokens]（手册令牌），形状/间距取自手册圆角阶梯
 * （R1=4 / R2=8 / R3=12 / R4=16 / Rfull）与间距阶梯（S1..S8，见 [UiSpace]）。
 *
 * 零件清单（对应定稿规范 D 编号）：
 *   - [SectionCard]      C-09 卡片（R3 + 1dp 描边 + 16dp 内边距）
 *   - [StatusBadge]      D-16 主页大状态徽标（C-11 放大档）
 *   - [DotTrack]         D-18 音量十档离散点轨（协议 0-9，吸附整数档）
 *   - [IconChip]         D-19/D-20 图标+文字横排 chip（音源 / 输入设备）
 *   - [ChipGroup]        chip 的 flex-wrap 容器
 *   - [LabeledSwitchRow] C-08 开关行（标签整行可点）
 *   - [ReadOnlyValueRow] D-21 只读值行（采样率）
 *   - [PortInputField]   D-21/C-06 控制端口输入框（高 56、右对齐等宽、聚焦 2dp accent）
 */

/**
 * 分区卡：R3 圆角 + 1dp 描边 + 16dp 内边距 + 可选小标题（标题→内容 12）。
 *
 * 背景取 ColorScheme.surface 而非令牌 bgCard：AMOLED 纯黑模式下 surface 被
 * 覆写为 #000000，卡片必须跟随（令牌 bgCard 只当 12% 混合公式的分母）。
 */
@Composable
fun SectionCard(
    title: String? = null,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            LocalMicLinkTokens.current.border
        )
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            if (title != null) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall,
                    color = LocalMicLinkTokens.current.text1
                )
                Spacer(Modifier.height(12.dp))
            }
            content()
        }
    }
}

/** 卡片内的分隔线（1dp border 令牌；两端留白由调用方布局决定）。 */
@Composable
fun CardDivider() {
    HorizontalDivider(color = LocalMicLinkTokens.current.border)
}

/**
 * 小号说明文字（12sp，默认 text-2）。
 *
 * @param color 覆盖文字色：用于把「输入被截断」这类需要用户注意的说明转 err 色
 *   （D-04），默认保持 text-2 不改变既有观感。
 */
@Composable
fun HintText(
    text: String,
    modifier: Modifier = Modifier,
    color: Color = LocalMicLinkTokens.current.text2
) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        color = color,
        modifier = modifier
    )
}

/**
 * 大状态徽标（D-16）：高 32、Rfull、圆点 8、文字 16sp Medium、语义色 12% 底。
 *
 * 三通道编码（P-1）：语义色（色）+ 实心圆点（形）+ 中文状态词（文）。
 * 文字由 [com.miclink.open.MicCaptureService.stateWord] 提供，与前台服务
 * 通知标题逐字一致（D-16 验收项）。
 *
 * @param color 语义色（圆点与文字）。
 * @param container 徽标底（= 语义色 12% 混合 bg-card，见 [MicLinkTokens]）。
 */
@Composable
fun StatusBadge(
    text: String,
    color: Color,
    container: Color,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .height(32.dp)
            .clip(CircleShape)
            .background(container)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .clip(CircleShape)
                .background(color)
        )
        Spacer(Modifier.width(6.dp))
        Text(
            text = text,
            style = MaterialTheme.typography.titleSmall.copy(
                fontSize = 16.sp,
                fontWeight = FontWeight.Medium
            ),
            color = color
        )
    }
}

/** 点轨档位数：协议音量 0-9 十档（手册 §7.2 硬事实，0 静音 / 5 原样 / 每档 ±6 dB）。 */
const val DOT_TRACK_STEPS: Int = 9

/**
 * 点轨 x 坐标 -> 档位（等分格 floor 取整，边缘也好命中）。
 *
 * 轨道宽 [width] 上均分 [DOT_TRACK_STEPS] + 1 个格位（0..9 共 10 档），
 * 结果恒为 0..9 的整数（协议只认整数档，禁连续浮点）。
 */
private fun indexFromX(x: Float, width: Int): Int {
    if (width <= 0) return 0
    return (x / width.toFloat() * (DOT_TRACK_STEPS + 1))
        .toInt()
        .coerceIn(0, DOT_TRACK_STEPS)
}

/**
 * 十档离散点轨（D-18）：主页推流音量控件。
 *
 * 规格（定稿图即验收）：
 *  - 10 档等分；未选档 12dp 圆（bg-sunken + 1dp border），选中档 16dp accent
 *    实心 + 2dp 白色内描边；
 *  - 档位数字 11sp text-2（选中档 accent Medium）；**0 档数字替换为「静音」**；
 *  - 点击档位即选，拖拽连续滑动但**吸附整数档**（禁连续浮点，P-4）；
 *  - 禁用态（静音联动）整轨降 text-3，不响应手势。
 *
 * @param value 当前档位（0..9，越界值收敛）。
 * @param onValueChange 档位变更（整数）。
 * @param enabled false 时置灰且不响应点按/拖拽。
 */
@Composable
fun DotTrack(
    value: Int,
    onValueChange: (Int) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    val tokens = LocalMicLinkTokens.current
    val current = value.coerceIn(0, DOT_TRACK_STEPS)

    // 手势（QA 缺陷 D-02 修复）：点按选档 + 横向拖拽吸附，且**必须消费**横向拖拽。
    //
    // 为什么必须 consume：主页外层是 ModalNavigationDrawer，抽屉手势挂在整个内容区
    // 上（没有"边缘窄带"限制）。旧实现只在 pointerInput 里读事件、从不消费，于是
    // 同一次横拖既改档位、又把抽屉拉出（QA dump 检出「抽屉=开」）。
    // detectHorizontalDragGestures 在越过 touchSlop 时会 consume 该事件，而子节点
    // 在 Main pass 中先于父节点处理 → 抽屉的 slop 检测看到 isConsumed 即取消本次
    // 手势。因此**不需要** gesturesEnabled = false（那会砍掉边缘滑出开抽屉的习惯）：
    // 只有落在点轨 48dp 区域内的横向拖拽被点轨吃掉，其余手势全部保持原样。
    val gestureModifier = modifier
        .fillMaxWidth()
        .height(48.dp)
        .pointerInput(enabled) {
            if (!enabled) return@pointerInput
            detectTapGestures { offset ->
                onValueChange(indexFromX(offset.x, size.width))
            }
        }
        .pointerInput(enabled) {
            if (!enabled) return@pointerInput
            detectHorizontalDragGestures(
                onDragStart = { offset ->
                    // 越过 touchSlop 即选：起手那一下也要有反馈（吸附整数档）
                    onValueChange(indexFromX(offset.x, size.width))
                },
                onHorizontalDrag = { change, _ ->
                    // 消费 = 声明"这次横拖归我"，抽屉边缘手势收不到
                    change.consume()
                    onValueChange(indexFromX(change.position.x, size.width))
                }
            )
        }

    Row(
        modifier = gestureModifier,
        verticalAlignment = Alignment.CenterVertically
    ) {
        for (index in 0..DOT_TRACK_STEPS) {
            val selected = index == current
            val dotColor = when {
                !enabled -> tokens.text3
                selected -> tokens.accent
                else -> tokens.bgSunken
            }
            val labelColor = when {
                !enabled -> tokens.text3
                selected -> tokens.accent
                else -> tokens.text2
            }
            Column(
                modifier = Modifier.weight(1f),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Box(
                    modifier = Modifier.size(16.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        modifier = Modifier
                            .size(if (selected) 16.dp else 12.dp)
                            .clip(CircleShape)
                            .background(dotColor)
                            .then(
                                if (selected) {
                                    // 选中档：2dp 白色内描边（Compose border 画在边界内侧）
                                    if (enabled) {
                                        Modifier.border(2.dp, Color.White, CircleShape)
                                    } else {
                                        Modifier.border(1.dp, tokens.border, CircleShape)
                                    }
                                } else {
                                    Modifier.border(1.dp, tokens.border, CircleShape)
                                }
                            )
                    )
                }
                Spacer(Modifier.height(4.dp))
                Text(
                    text = if (index == 0) {
                        stringResource(R.string.dot_track_mute)
                    } else {
                        index.toString()
                    },
                    style = MaterialTheme.typography.labelSmall.copy(fontSize = 11.sp),
                    fontWeight = if (selected) FontWeight.Medium else FontWeight.Normal,
                    color = labelColor,
                    maxLines = 1
                )
            }
        }
    }
}

/**
 * 图标 + 文字横排 chip（D-19/D-20：音源六档 / 输入设备）。
 *
 * 规格：高 32、R2、左右内边距 6、图标 16 + 间隙 6 + 标签 14sp Medium；
 * 选中 = accent 底 + 白字白图标；未选 = bg-sunken + text-1 + 1dp border；
 * 禁用 = text-3（可见不可点，禁用原因由调用方用 [HintText] 说明，X-12）。
 */
@Composable
fun IconChip(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    val tokens = LocalMicLinkTokens.current
    val shape = RoundedCornerShape(8.dp)
    val container = if (selected && enabled) tokens.accent else tokens.bgSunken
    val content = when {
        !enabled -> tokens.text3
        selected -> tokens.onAccent
        else -> tokens.text1
    }
    val showBorder = !(selected && enabled)

    Row(
        modifier = modifier
            .height(32.dp)
            .clip(shape)
            .background(container)
            .then(
                if (showBorder) Modifier.border(1.dp, tokens.border, shape) else Modifier
            )
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(16.dp),
            tint = content
        )
        Spacer(Modifier.width(6.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
            color = content,
            maxLines = 1
        )
    }
}

/**
 * chip 组的 flex-wrap 容器（M-02：横排、间距 8、窄屏自动折行不破版）。
 *
 * 360dp 最小设计宽度下音源六档呈 3+3 两行（定稿图验收形态）。
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ChipGroup(
    modifier: Modifier = Modifier,
    content: @Composable FlowRowScope.() -> Unit
) {
    FlowRow(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        content = content
    )
}

/**
 * 开关行（C-08）：左标签（+可选说明）右开关；标签整行可点。
 *
 * @param enabled false 时开关禁用（调用方负责给原因文字，X-12）。
 */
@Composable
fun LabeledSwitchRow(
    title: String,
    description: String?,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    val tokens = LocalMicLinkTokens.current
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(enabled = enabled) { onCheckedChange(!checked) },
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge,
                color = if (enabled) tokens.text1 else tokens.text3
            )
            if (description != null) {
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = tokens.text2
                )
            }
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            enabled = enabled
        )
    }
}

/**
 * 只读值行（D-21 采样率）：左标签 + 右端只读值（不可点、无输入框，
 * 工程适配：协议协商在 Windows 端发起，app 侧不可选——禁止做成假开关）。
 *
 * @param labelWidth 标签列宽（与端口输入框对齐）。
 */
@Composable
fun ReadOnlyValueRow(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    labelWidth: Dp = 72.dp
) {
    val tokens = LocalMicLinkTokens.current
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = tokens.text1,
            modifier = Modifier.width(labelWidth)
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium.copy(fontSize = 13.sp),
            color = tokens.text1
        )
    }
}

/**
 * 控制端口输入框（D-21 / C-06 And 档）：高 56、R2、bg-sunken、1dp border、
 * 右对齐等宽数字、聚焦 2dp accent 描边；错误态 err 描边（错误文字由调用方给）。
 *
 * @param isError 端口非法（1-65535 之外）时描边转 err。
 * @param enabled 运行中禁用（端口启动时锁定；调用方给原因文字）。
 */
@Composable
fun PortInputField(
    value: String,
    onValueChange: (String) -> Unit,
    enabled: Boolean,
    isError: Boolean,
    modifier: Modifier = Modifier
) {
    val tokens = LocalMicLinkTokens.current
    var focused by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(8.dp)
    val borderColor = when {
        !enabled -> tokens.border
        focused -> tokens.accent
        isError -> tokens.err
        else -> tokens.border
    }
    val borderWidth = if (focused && enabled) 2.dp else 1.dp

    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        enabled = enabled,
        singleLine = true,
        textStyle = TextStyle(
            fontFamily = FontFamily.Monospace,
            fontSize = 16.sp,
            textAlign = TextAlign.End,
            color = if (enabled) tokens.text1 else tokens.text3
        ),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        cursorBrush = SolidColor(tokens.accent),
        modifier = modifier
            .height(56.dp)
            .clip(shape)
            .background(tokens.bgSunken)
            .border(borderWidth, borderColor, shape)
            .onFocusChanged { focused = it.isFocused }
            .padding(horizontal = 16.dp),
        decorationBox = { innerTextField ->
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.CenterEnd
            ) {
                innerTextField()
            }
        }
    )
}

/** 统一的区块间距（手册 §4.2 间距阶梯 S1..S8，与 Windows sd() 阶梯对应）。 */
object UiSpace {
    val xs: Dp = 4.dp
    val sm: Dp = 8.dp
    val md: Dp = 12.dp
    val lg: Dp = 16.dp
    val xl: Dp = 24.dp
}
