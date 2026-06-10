# Mac AI Agent + ESP32 XiaoZhi 集成方案

> 初次讨论日期：2026-06-09
> 状态：需求讨论阶段，未进入开发

## 1. 项目目标

Mac 系统上运行 AI Agent（支持定时任务、飞书/微信接入），与 ESP32-S3-CAM 上的小智语音助手协同工作，实现：

- Mac 远程触发 ESP32 摄像头拍照
- 小智（云端 AI）和 Mac AI Agent 双重视觉分析
- 分析结果推送到飞书/微信
- 飞书/微信消息可通过小智语音播报

## 2. 典型场景

### 场景A：定时巡检

```
Mac cron 触发 → Mac Agent → ESP32 拍照 → 照片返回 Mac
→ Mac AI Vision 分析 → 结果推送飞书/微信
```

### 场景B：聊天触发拍照

```
用户在飞书发"看看家里" → Mac Agent 接收 → ESP32 拍照
→ 照片返回 Mac + 小智 AI 描述 → Mac AI 综合分析 → 回复飞书
```

### 场景C：语音播报

```
重要告警/消息 → Mac Agent → ESP32 TTS → 小智喇叭语音播报
```

### 场景D：双向对话

```
用户在微信发消息 → Mac Agent → ESP32 → 小智语音回复
→ 回复文本/音频返回 Mac → 转发微信
```

## 3. 整体架构

```
┌─────────────────────────────────────────────────┐
│                  Mac AI Agent                    │
│  ┌──────┐  ┌──────────┐  ┌───────┐  ┌────────┐ │
│  │ 定时  │  │ 飞书Bot  │  │微信Bot│  │ AI视觉 │ │
│  │ 任务  │  │          │  │       │  │ 分析   │ │
│  └──┬───┘  └────┬─────┘  └───┬───┘  └───▲────┘ │
│     └───────────┼────────────┼──────────┘       │
│                 │  Agent 编排   │                  │
│            ┌────▼────────────▼─────┐             │
│            │   通信网关 (HTTP/WS)   │             │
│            └────────────┬──────────┘             │
└─────────────────────────┼────────────────────────┘
                          │  WiFi (局域网)
┌─────────────────────────┼────────────────────────┐
│  ESP32 XiaoZhi          │                        │
│  ┌──────────┐  ┌────────▼───────┐  ┌──────────┐ │
│  │ 摄像头    │  │  HTTP Server   │  │ TTS 引擎  │ │
│  │ OV5640   │  │  /capture      │  │ (小智语音)│ │
│  │          │  │  /tts          │  │          │ │
│  └──────────┘  └────────────────┘  └──────────┘ │
└──────────────────────────────────────────────────┘
```

## 4. ESP32 端 API 设计

### 4.1 HTTP REST 接口（Mac 主动调用 ESP32）

#### GET /capture

拍照并返回 JPEG 图片。

- 请求：`GET /capture?resolution=vga` （可选分辨率：qvga, vga, svga）
- 响应：`Content-Type: image/jpeg`，body 为 JPEG 二进制数据
- 备注：拍照时暂停音频管线（WakeNet/DOA）以释放内存，拍完后恢复

#### POST /query

发送文字指令，让小智 AI 处理（跳过唤醒词和 ASR，直接走云端）。

- 请求：`POST /query`，body `{"text": "描述你看到的"}`
- 响应：`{"reply": "我看到桌子上有一个杯子...", "photo_taken": true, "photo_url": "/capture"}`
- 备注：可结合拍照，让小智对照片进行 AI 描述

#### POST /tts

让小智语音播报一段文字（TTS-only 模式，跳过唤醒词和 ASR）。

- 请求：`POST /tts`，body `{"text": "你有一条新消息"}`
- 响应：`{"status": "playing"}`
- 备注：需要从小智对话流程中抽取 TTS 管线作为独立接口

### 4.2 WebSocket 接口（双向推送）

用于需要 ESP32 主动通知 Mac 的场景，以及 Mac 异步推送 TTS 指令。

- 连接：`ws://<esp32-ip>/ws`
- Mac → ESP32 消息类型：
  - `{"type": "tts", "text": "xxx"}` — 语音播报
  - `{"type": "capture"}` — 拍照（结果通过 WS 返回，base64 编码）
- ESP32 → Mac 消息类型：
  - `{"type": "alert", "event": "sound_detected", "angle": 45}` — 声音事件通知
  - `{"type": "photo", "data": "<base64 jpeg>"}` — 拍照结果

### 4.3 USB Serial Fallback（离线降级）

当 WiFi 不可用时，Mac 通过 USB Serial JTAG 发送简单命令：

- `CMD:WAKE\n` — 唤醒小智
- `CMD:CAPTURE\n` — 拍照（通过串口分帧协议传回 JPEG）
- `CMD:TTS:你好\n` — 语音播报

仅用于命令触发，不用于大文件传输。

## 5. Mac AI Agent 设计

### 5.1 技术栈

| 组件 | 建议 | 说明 |
|---|---|---|
| 语言 | Python 3.11+ | |
| Agent 框架 | LangGraph / 自定义 | 编排多步骤工作流 |
| 定时任务 | APScheduler / cron | 支持 cron 表达式和间隔触发 |
| 飞书接入 | 飞书开放平台 Bot SDK | 官方 Python SDK |
| 微信接入 | 企业微信 Bot API / itchat | 企业微信更稳定，个人号用 itchat |
| AI 视觉 | Claude API (Vision) / GPT-4o | Mac 端二次图像分析 |
| ESP32 通信 | requests (HTTP) + websockets | |
| 持久化 | SQLite | 存历史照片、分析结果、消息记录 |

### 5.2 Agent 工作流

```
[触发源] → [任务解析] → [ESP32 操作] → [AI 分析] → [结果分发]
   │                          │               │             │
   ├─ 定时任务                 ├─ 拍照          ├─ 小智描述    ├─ 飞书消息
   ├─ 飞书消息                 ├─ 语音播报      ├─ Mac AI     ├─ 微信消息
   └─ 微信消息                                 └─ 综合汇总    └─ ESP32 语音
```

### 5.3 健康检查

- 定期 ping ESP32 HTTP Server，检测设备在线状态
- 异常时通过飞书/微信发送告警
- ESP32 端启用看门狗，异常自动重启

## 6. 双 AI 视觉分析

同一张照片可以经两条 AI 管线并行分析：

```
管线A（小智看）：ESP32 拍照 → XiaoZhi 云端 AI → 中文场景描述
管线B（Mac看）：ESP32 拍照 → JPEG 返回 Mac → Claude/GPT Vision → 专业分析
```

| 管线 | 优势 | 适合场景 |
|---|---|---|
| 小智云端 AI | 中文场景理解优化，低延迟 | 日常描述、简单问答 |
| Mac AI Agent | 更强的视觉能力（OCR、物体检测、细节分析） | 专业分析、告警判断 |

## 7. 注意事项与约束

### ESP32 内存

- HTTP Server + 摄像头 + XiaoZhi 音频管线同时运行，内存紧张
- 拍照时暂停音频管线（WakeNet/DOA），释放 PSRAM
- HTTP Server 使用单连接模式，不支持并发
- 图片分辨率按需选择：监控用 QVGA（小），AI 分析用 VGA/SVGA

### ESP32 稳定性

- 7x24 运行需看门狗 + 异常自动重启
- Mac Agent 端做健康检查和重连机制

### 安全性

- 局域网 HTTP 无认证，内网使用可接受
- 若网络环境复杂，建议加 API Key 或 token 认证

### 网络

- ESP32 和 Mac 需在同一局域网
- 小智 AI 功能依赖外网（XiaoZhi 云端）
- USB Serial 作为 WiFi 断线时的 fallback

### 小智 TTS 约束

- TTS 走云端，不能离线使用
- 需要从现有对话流程中抽取独立的 TTS 接口
- 播报时与正常对话互斥，需队列管理

## 8. 建议开发顺序

| 阶段 | 内容 | 优先级 |
|---|---|---|
| Phase 1 | ESP32 HTTP `/capture` + Mac Agent 定时拍照 + 飞书推送 | 最高 |
| Phase 2 | Mac AI Vision 分析 + 综合结果推送 | 高 |
| Phase 3 | ESP32 `/tts` + WebSocket 双向通信 | 中 |
| Phase 4 | ESP32 `/query`（小智 AI 分析照片）| 中 |
| Phase 5 | 微信接入 + USB Serial fallback | 低 |
| Phase 6 | 健康检查 + 看门狗 + 异常恢复 | 低 |

## 9. 待讨论问题

- [ ] Mac AI Agent 框架选型：LangGraph vs 自定义 vs 其他
- [ ] 飞书 vs 微信优先级：先接哪个
- [ ] 图片分辨率默认值：VGA 够用还是需要 SVGA
- [ ] ESP32 端是否需要鉴权（token / API key）
- [ ] 照片历史存储策略：保留多久、存哪里
- [ ] 并发处理：小智正在语音对话时，Mac Agent 发来拍照请求如何处理
- [ ] 成本估算：Claude/GPT Vision API 调用频率和费用
