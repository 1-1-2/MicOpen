package com.miclink.open.ui.component

import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miclink.open.EventLog
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collect

/** Immutable paused snapshot keeps text selection stable while logs continue. */
@Composable
fun ConnectionLogCard(snapshot: EventLog.Snapshot, onClear: () -> EventLog.Snapshot) {
    val context = LocalContext.current
    val tokens = LocalMicLinkTokens.current
    var paused by remember { mutableStateOf(false) }
    var frozen by remember { mutableStateOf(snapshot) }
    val shown = if (paused) frozen else snapshot
    val scroll = rememberScrollState()
    var followLatest by remember { mutableStateOf(true) }
    var userScrollVersion by remember { mutableLongStateOf(0L) }
    val scrolling by remember { derivedStateOf { scroll.isScrollInProgress } }
    LaunchedEffect(scroll) {
        scroll.interactionSource.interactions.collect { interaction ->
            if (interaction is DragInteraction.Start) {
                followLatest = false
                userScrollVersion++
            }
        }
    }
    LaunchedEffect(userScrollVersion, scrolling, paused, followLatest) {
        if (!paused && !scrolling && !followLatest) {
            delay(20_000)
            followLatest = true
        }
    }
    LaunchedEffect(shown.version, scroll.maxValue, followLatest, paused) {
        if (!paused && followLatest) scroll.scrollTo(scroll.maxValue)
    }
    SectionCard {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("运行日志", style = MaterialTheme.typography.titleSmall,
                modifier = Modifier.weight(1f), maxLines = 1)
            TextButton(onClick = {
                frozen = onClear()
                followLatest = true
            }, contentPadding = PaddingValues(horizontal = 6.dp)) { Text("清除", maxLines = 1) }
            TextButton(onClick = {
                if (!paused) frozen = snapshot
                paused = !paused
                if (!paused) followLatest = true
            }, contentPadding = PaddingValues(horizontal = 6.dp)) { Text(if (paused) "继续" else "暂停", maxLines = 1) }
            TextButton(enabled = shown.text.isNotEmpty(), onClick = {
                val clipboard = context.getSystemService(ClipboardManager::class.java)
                if (clipboard != null) {
                    clipboard.setPrimaryClip(ClipData.newPlainText("MicLink 运行日志", shown.text))
                    Toast.makeText(context, "已复制 ${shown.count} 条日志", Toast.LENGTH_SHORT).show()
                }
            }, contentPadding = PaddingValues(horizontal = 6.dp)) { Text("复制", maxLines = 1) }
        }
        Box(Modifier.fillMaxWidth()) {
            Box(Modifier.fillMaxWidth().heightIn(min = 100.dp, max = 260.dp)
                .background(tokens.bgSunken, MaterialTheme.shapes.small)
                .verticalScroll(scroll).padding(10.dp)) {
                SelectionContainer {
                    Text(shown.text.ifEmpty { "暂无运行日志" }, color = tokens.text2,
                        fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 17.sp)
                }
            }
            // Use the card's existing 16dp bottom inset; reserve no extra row.
            Text("长按可复制", color = tokens.text2, fontSize = 9.sp, lineHeight = 12.sp,
                modifier = Modifier.align(Alignment.BottomEnd).offset(y = 14.dp).padding(end = 2.dp))
        }
    }
}
