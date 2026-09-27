# image-client — C++/QML 重建规格（v0.1, 2026-09-27）

参考实现：<https://github.com/HarriethWiKk/openai-image-client>（下称"旧仓"；本规格中的 `file:line` 均指其 `main` 分支）
本文件是**规格**，不是实现说明。凡标注 `[冻结]` 的条目是实现必须逐条满足的行为；标注 `[推算]` 的是尚未实测的工程估计。

---

## 1. 范围（已锁定）

| 项 | 决定 |
|---|---|
| 形态 | Windows 桌面应用，C++ + QML |
| 功能 | 文生图（generate）、图生图（edit）。**不含视频生成与播放** |
| Provider | OpenAI 兼容 Images API、xAI Grok Imagine（图像）、Google Gemini `generateContent` |
| 浏览器路径 | 无。不实现任何 HTTP 服务、不监听端口 |
| MCP | 仅 `stdio`，供本机 agent 拉起。不实现 Streamable-HTTP / 多租户 |
| CLI | 保留在范围内但**不在主线**，主线完成后另启 |
| 许可 | **Apache-2.0**。Qt 按 LGPLv3 动态链接，第三方归属见 `THIRD_PARTY_NOTICES.md`。注意"可静态链接"这一条已被实测推翻：官方 Qt 二进制分发只有 shared 库（§11.1） |
| 旧代码 | 代码不移植；协议行为按本规格移植 |

明确不做：视频、多租户、远程访问、OAuth/动态注册、Linux/macOS、pip/npm 可安装包。

---

## 2. 架构

### 2.1 模块与构建产物

```
core/  (静态库 liboic-core.a)
  protocol/    端点解析、协议判定、body 构造、响应解析
  net/         HTTP 客户端、SSRF 校验、连接固定、重定向控制
  store/       sqlite（任务/历史/profile）、asset 落盘、配额
  secret/      Windows Credential Manager 读写
  jobs/        任务状态机、调度、取消、TTL
apps/
  gui/         QML 桌面应用   → ImageClient.exe      （WIN32 子系统）
  mcp/         stdio MCP 服务 → image-client-mcp.exe （console 子系统）
```

**[冻结] 两个可执行文件，禁止合并为单二进制的子命令模式。** 理由：console exe 才能安全持有 stdout；GUI exe 的 `qDebug`/`qWarning` 一旦混入 JSON-RPC 的 stdout 帧，协议即损坏且极难定位。

**[冻结] `mcp` 目标不得链接 QtQuick / QtQML / QtGui，只依赖 QtCore + core。** 因此它从结构上无法触碰托盘与单实例锁。

**[冻结] 单实例锁（`QLockFile`）只在 `gui` 目标实现**，锁键取安装目录哈希。托盘（`QSystemTrayIcon`）同属 `gui`。旧仓 `app_desktop.py` 的 330 行 ctypes 实现整体废弃，不参考其代码，只参考它踩过的坑（见 §10）。

### 2.2 依赖预算

| 用途 | 选型 | 说明 |
|---|---|---|
| UI | Qt6 Quick + QuickControls2（**Basic 风格**，`QQuickStyle::setStyle("Basic")` 显式钉死） | Qt **6.8.3** msvc2022_64 |
| HTTP/TLS | Qt Network（`QNetworkAccessManager`） | 需 `plugins/tls` 后端 |
| 图像解码 | QtGui `QImageReader` | 替代 Pillow，见 §6.4 的强制开关 |
| 存储 | SQLite（Qt Sql 或直连 sqlite3） | 新能力 |
| JSON | Qt Core `QJsonDocument` | |
| MCP | **自研 stdio 帧与 JSON-RPC 分派，不引入第三方 MCP 库** | stdio 无网络攻击面；官方无 C++ SDK，社区最强者仅 147 星 / 88 未关闭 issue，不足以作为长期依赖 |
| 构建 | CMake 4.4.3 + Ninja 1.13.2 + MSVC 14.51（VS Community 2026） | |

### 2.3 工具链基线（2026-09-27 实测通过，非假设）

| 组件 | 要求 |
|---|---|
| Qt | 6.8.3，**msvc2022_64** 二进制构建。需含 Quick / Qml / Gui / Core / Network / **Sql** / QuickControls2，且 `qml/QtQuick/Controls` 下有 Basic 风格；安装位置不限 |
| 编译器 | MSVC 14.51（Visual Studio 2026 Community 的 C++ 工作负载），需 `vcvars64.bat` |
| Windows SDK | 10.0.26100 |
| 构建工具 | CMake 4.4.3 + Ninja 1.13.2（版本为下限，安装方式不限；测试注册依赖 Qt bin 在 PATH 上，见 `tests/CMakeLists.txt`） |

`scripts/build.cmd` 要求 `QTDIR`（Qt 前缀），可选 `CMAKE_DIR`（cmake/ninja 不在 PATH 上时用）；Visual Studio 与 C++ 工作负载由 `vswhere.exe` 探测，脚本内**不含任何本机绝对路径**，缺项时给出明确报错。

**产物直接落在 `build/` 根目录**（`build/ImageClient.exe`、`build/image-client-mcp.exe`），不在 `build/bin` —— §11 的打包脚本要按这个布局写。

已实测的兼容性结论：MSVC 14.51（VS 2026 工具集）链接 Qt 6.8.3 的 msvc2022 构建**可用** —— 完整构建通过、QML AOT 编译通过、GUI 正常启动且日志无 QML 报错、`ctest` 62 条断言全绿。

---

## 3. 常量与上限 `[冻结]`

移植自旧仓 `web_app.py:97-140`、`image_client_core/responses.py:30-32`。**这些数值不是风格选择，每一条都对应一次线上故障**，修改需在此文件留理由。

```
DEFAULT_IMAGE_MODEL              gpt-image-2
DEFAULT_TIMEOUT_SECONDS          500      MIN 5   MAX 600
MAX_IMAGE_COUNT (n)              8        (MCP 侧 4，CLI 侧 10 —— 见 §8.2)
MAX_UPSTREAM_MEDIA_ITEMS         16       单次响应采纳的图片数硬上限
MAX_PARTIAL_IMAGES               3        DEFAULT_STREAM_PARTIAL_IMAGES = 2
MAX_PROMPT_CHARS                 16000
MAX_MODEL_CHARS                  200
MAX_BASE_URL_CHARS               2048
MAX_API_KEY_CHARS                8192
MAX_REFERENCE_IMAGE_BYTES        10 MiB   单张参考图
MAX_REFERENCE_IMAGES_BYTES       30 MiB   参考图合计
MAX_EDIT_REFERENCE_IMAGES        16       张数上限（Grok 路径另有更严限制，见 §5.3）
MAX_REMOTE_MEDIA_BYTES           64 MiB   单张结果图（4K PNG base64 解码后会超 20 MiB）
MAX_GENERATED_MEDIA_BYTES        96 MiB   单次任务结果合计
MAX_UPSTREAM_JSON_BYTES          128 MiB  上游 JSON 响应体
DIAGNOSTIC_BODY_BYTES            64 KiB   诊断只需错误体头部
MAX_SSE_EVENT_BYTES              28 MiB
MAX_RETAINED_RAW_BYTES           64 KiB   留在诊断里的原始响应
MAX_REDIRECTS                    3
JOB_TTL_SECONDS                  3600
MAX_JOB_RUNTIME_SECONDS          900
CONCURRENT_JOBS                  2        PENDING_JOBS 8   MAX_JOBS 64
```

任务与历史的持久化取代旧仓的纯内存实现，因此 `MAX_JOBS`/`JOB_TTL_SECONDS` 的语义变为**内存中活跃任务**的上限，历史行不受此约束（见 §6.2）。

---

## 4. 端点解析 `[冻结]`

移植自 `image_client_core/endpoints.py`。

`KNOWN_API_ENDPOINTS`（本项目只需图像子集）：
```
/v1/images/generations   /images/generations
/v1/images/edits         /images/edits
```

`candidate_endpoints(base_url, route)` 的判定顺序，必须逐分支实现：

1. `normalize_base_url` = `strip()` + 去尾部 `/`。
2. 若 base 以某个 `KNOWN_API_ENDPOINTS` 项结尾 → **只返回一个**候选，即把该 endpoint 从 base 剥掉后重新拼接；剥掉的是 `/v1/...` 形式则保留 `v1/` 前缀。
3. 若 base 以 `/v1` 结尾 → 单一候选 `base + route`。
4. 否则 → **两个候选**，顺序为 `base + /v1/ + route`，然后 `base + / + route`。

失败换下一个候选的判定（`responses.py:164-187`）：

- 继续尝试：`status >= 500`，或响应 JSON 的 `error.type` 含 `"upstream"`。
- **停止尝试**：`400 <= status < 500` 且 `status ∉ {404, 405}` 且 `Content-Type` 不含 `text/html`。
  `[理由]` 旧实现在任何 400 之后会把整个 multipart（单张参考图可达 50 MB）向第二个候选端点重发一次——浪费带宽，且当第一个请求真的到达了 provider 时构成**重复计费**。
- 其余情况按 `retryable` 处理。

`route` 从不硬编码完整 URL；一切拼接经 `join_url`（去重 `/`）。

---

## 5. Provider 协议规格

### 5.1 协议判定 `web_app.py:620-631` `[冻结]`

`protocol ∈ {auto, openai, grok, gemini}`，非法值报错。非 `auto` 时直接采信。`auto` 时按序：

1. `is_grok_image_model(model)` 或 base 含 `api.x.ai` / `x.ai` → `grok`
2. `model` 以 `gemini-` 开头 或 base 含 `generativelanguage.googleapis.com` → `gemini`
3. 否则 → `openai`

### 5.2 模型识别 `image_client_core/models.py` `[冻结]`

**所有模型名比较必须先 `strip().lower()`。**

```
is_gpt_image_2(m):  m == "gpt-image-2" || m.startswith("gpt-image-2-") || m.startswith("gpt-image-2.5")
GROK_IMAGE_GENERATION_MODELS = {grok-imagine-edit, grok-imagine-image, grok-imagine-image-lite,
                                grok-imagine-image-pro, grok-imagine-image-quality}
is_grok_image_model(m): m == "grok-imagine" || 任一集合项满足 m == item || m.startswith(item + "-")
```

`[理由，必须保留]` 旧 CLI 曾逐字比较 `model == "gpt-image-2"`，于是 `--model GPT-Image-2` 静默走了错误分支：edit 用了 `image` 而非 `image[]` 字段，并跳过 `background=transparent` 拒绝，最终收到一个无法解释的上游 400。

Grok 尺寸推导：
```
grok_resolution_from_size(size):   解析失败 → "1k"；max(w,h) > 1536 → "2k"，否则 "1k"
grok_aspect_ratio_from_size(size): 解析失败 → "1:1"；否则取最接近的支持比例
closest_ratio: 先算 gcd 精确比例，命中集合则直接返回；否则在集合内取 |w/h − 候选| 最小者，排除 "auto"
GROK_SUPPORTED_RATIOS = {1:1,16:9,9:16,4:3,3:4,3:2,2:3,2:1,1:2,19.5:9,9:19.5,20:9,9:20,auto}
GROK_RESOLUTION_VALUES = {"",1k,2k}   GROK_RESPONSE_FORMAT_VALUES = {"",url,b64_json}
```

### 5.3 OpenAI 兼容路径

**generation**：JSON body `{model, prompt, size, n}` + §5.5 的公共选项，POST `images/generations`。

**edit（multipart/form-data）** `web_app.py:1862-1916`：

- 表单字段值全部转字符串（含 `n`）。
- 文件字段名：`is_gpt_image_2(model) || 参考图数 > 1` → `image[]`，否则 `image`。`[冻结]`
- 参考图来源三选一（按存在性优先）：`data_url` / `dataUrl` / `url`。data URL 必须是 `;base64`，`Content-Type` 必须 `image/*`，否则报错。
- `edit_images` 允许是 list 或单个 dict（旧字段 `edit_image` 为 dict 时也接受）；空则报"图生图模式下请先上传参考图"；超过 16 张报错。
- 逐张累计字节，超 30 MiB 报错（**不是**逐张 30 MiB）。

### 5.4 Grok 路径

`build_grok_generation_body`（JSON，POST `images/generations`）：
```
{model, prompt, n, aspect_ratio, resolution}   // response_format 非空时才带上
aspect_ratio 缺省时由 size 推导；resolution 缺省时由 size 推导
```

`build_grok_edit_body`（JSON，POST `images/edits`）**而非 multipart**：
```
参考图 → {"type":"image_url","url":<data_url 或 http(s) URL>}
单张 → 字段 "image"（对象）；多张 → 字段 "images"（数组）
Grok 图生图最多 3 张参考图            [冻结]（严于 §5.3 的 16）
aspect_ratio / resolution 仅在「多张 或 用户显式指定」时发送
```

### 5.5 公共输出选项 `apply_openai_image_options` `[冻结]`

```
quality / background / moderation : 非空即透传
background == "transparent" && is_gpt_image_2(model) → 拒绝，提示改 auto 或 opaque
output_format : 非空即透传
output_compression : 仅当 output_format ∈ {jpeg, webp} 才发送；必须是 0..100 整数，否则报错
size : 自由形式 "WxH"，仅做长度 ≤64 校验，不校验白名单（三家各自决定接受范围）
```

**OpenAI 路径不发送 `response_format`**（旧行为，保持不变）：结果字段按 §7 的优先级容错解析。

### 5.6 Gemini 原生路径 `web_app.py:634-651, 913-992`

端点构造 `gemini_generate_endpoint(base, model)`：
1. base 已以 `:generateContent` 结尾 → 原样。
2. 剥掉 model 的 `models/` 前缀，按 RFC 3986 unreserved 集 `-._~` 做 percent-encode。
3. 若 base 最后一段路径 ∉ `{v1, v1beta, v1alpha}` → 追加 `/v1beta`。
4. 结果 `{base}/models/{encoded}:generateContent`。

请求体：
```json
{"contents":[{"role":"user","parts":[{"text":PROMPT}, ...]}],
 "generationConfig":{"responseModalities":["TEXT","IMAGE"],
                     "imageConfig":{"aspectRatio":"1:1","imageSize":"1K"}}}
```
- edit 模式：把解码后的参考图依次追加为 `{"inlineData":{"mimeType":..,"data":<b64>}}` part —— **没有独立 edits 端点**。
- `aspect_ratio ∈ {1:1,2:3,3:2,3:4,4:3,9:16,16:9,21:9}`（缺省 `1:1`），`imageSize ∈ {1K,2K,4K}`（缺省 `1K`，输入转大写）。
- 鉴权 `[冻结]`：请求头 `x-goog-api-key: <key>`（旧仓 `web_app.py:1019`）。**不得改为 `?key=` 查询参数** —— 那会把密钥写进每一层代理与访问日志。
- 要求 model 非空（`protocol == "gemini"` 且 model 为空时报错）。
- 需要 n 张时按 n 次串行请求模拟（旧行为）。响应解析见 §7.2。

---

## 6. 存储

### 6.1 Asset 落盘目录

**[冻结] 不得用系统临时目录。** 旧仓 `ASSET_ROOT = tempfile.gettempdir()/openai-image-client-assets/<job>`（`web_app.py:140`）且创建目录未指定 mode，等于世界可读。

新实现：`%LOCALAPPDATA%\image-client\assets\<job-id>\`，依赖 `%LOCALAPPDATA%` 默认 ACL（仅当前用户 + SYSTEM）；仍需显式校验并记录实际 ACL。**同时修复旧仓的"无跨进程锁"缺陷**：asset 目录创建与配额扣减必须在同一把命名互斥量下完成，允许 GUI 与 MCP 两个进程共存。

### 6.2 sqlite schema 草案 `[设计提案，非冻结]`

新能力——旧仓重启即失忆（`MAX_JOBS=64` 静默淘汰、无分页），本条是本次重建最主要的功能增量。

```sql
PRAGMA user_version = 1;
profiles(name TEXT PK, base_url TEXT NOT NULL, protocol TEXT NOT NULL,
         image_model TEXT, allowed_models TEXT, timeout_s INTEGER,
         credential_target TEXT NOT NULL, created_at INTEGER);
jobs(id TEXT PK, created_at, updated_at, mode TEXT, protocol TEXT, profile TEXT,
     model TEXT, prompt TEXT, size TEXT, n INTEGER, status TEXT,
     endpoint TEXT, client_request_id TEXT, error TEXT, duration_ms INTEGER,
     request_json TEXT, result_json TEXT,
     pinned INTEGER DEFAULT 0, deleted_at INTEGER);
assets(id TEXT PK, job_id REFERENCES jobs, ordinal INTEGER, filename TEXT,
       rel_path TEXT, bytes INTEGER, mime TEXT, width INTEGER, height INTEGER,
       sha256 TEXT, created_at INTEGER);
CREATE INDEX idx_jobs_created ON jobs(created_at DESC);
```
历史 UI 以 `jobs` 为唯一数据源，`pinned` 与软删除提供旧仓完全没有的保留控制。保留上限（默认建议 500 行 + 2 GiB 配额）作为设置项。

### 6.3 凭据

**[冻结] 唯一存储位置 = Windows Credential Manager。** 不再有 `secrets.json`，不再有 `chmod 600`（该调用在 NT 上只影响只读位，旧仓 `web_app.py:207` 自己注释了"尽力而为"）。

```
CredType  = CRED_TYPE_DOMAIN_GENERIC_PASSWORD
TargetName = "image-client/profile/<profile-name>"
CredentialBlob = UTF-8 密钥原文（无 BOM，无换行）
```
- profile 的 `credential_target` 存 TargetName；密钥永不写入 sqlite、日志、请求 URL。
- 不做文件回退。**回退等于把刚消掉的明文问题请回来。**
- 上游请求头：OpenAI/Grok 用 `Authorization: Bearer <key>`，Gemini 用 `x-goog-api-key`。

### 6.4 图像解码的强制安全开关 `[冻结]`

旧仓用 Pillow 时 `Image.DecompressionBombError` 是**默认开启**的（`assets.py:176` 显式捕获）。Qt 侧对应能力**默认关闭**，必须显式设置：
```cpp
QImageReader r(...);
r.setAllocationLimit(0);          // 0 = 默认 2 GiB 上限；禁止设为 SIZE_MAX
r.setImageCountLimit(1);          // 拦 GIF/TIFF 多帧放大
```
解码仅用于取尺寸与校验"确实是图片"，结果图字节原样落盘，不做重编码。

---

## 7. 响应解析与结果规范化

### 7.1 OpenAI 兼容 / Grok 共用 `web_app.py:778-820`

顶层必须 `data: [...]` 且非空，否则错误消息 `"接口返回异常，未找到 data"`，并附带 `compact_raw_response` 后的原始响应（截断至 64 KiB，且**必须剔除图像字节**）。

单条目按优先级取第一个命中字段，`[冻结]`：

1. `b64_json` → 有界严格 base64 解码（上限 64 MiB），MIME 由 `image_mime_from_format(item["output_format"])` 推得（jpeg/webp/gif/png，默认 png）→ 转 data URL。
2. `url` → 尝试下载为 data URL（走 §8.3 的 SSRF 校验与 64 MiB 上限）；**下载抛网络异常时回退为原始 URL 字符串**（旧行为，保留）。
3. `result` → `stream_image_data_url`，可为 data URL 或远程 URL。
4. 三者皆无 → 该条目跳过；全部条目都不可用 → 错误 `"返回里没有可保存的 b64_json、url 或 result"`。

累计字节超 `MAX_GENERATED_MEDIA_BYTES` → 立即报错，不截断返回。条目数超 `MAX_UPSTREAM_MEDIA_ITEMS` → 截断采纳。

### 7.2 Gemini

遍历 `candidates[].content.parts[]`，取 `inlineData`（**兼容 `inline_data` 拼写**）且 `mimeType` 以 `image/` 开头者，有界解码 → data URL。零张图片时的错误消息固定为 `"Gemini 返回中没有可用的图片，可能被安全策略拦截或模型不支持生图"`，并附红acted 原始响应：必须把 `inlineData.data` 的值替换成 `"<omitted>"` 再进诊断，否则 32 MB 图像会进错误日志。

### 7.3 base64 解码规则 `[冻结]` `responses.py:80-100`

```
1. 先用 \s+ 全局去空白（MIME 折行的合法换行要能过）
2. 长度 > 4*ceil(limit/3)+4 → 直接拒绝，不解码
3. base64 严格校验（等价 validate=True）：非法字符必须报错
4. 解码后长度 > limit → 报错
```
`[理由，不可省略第 3 步]` 非严格模式会静默丢弃字母表外字符，于是一个 HTML 错误页能被"成功解码"成一张看起来合法的垃圾图片写进历史。

### 7.4 响应体读取 `[冻结]` `responses.py:35-77`

- 先看 `Content-Length`，超限立刻关闭连接并报错；否则按 64 KiB 分块累计，越限即断。
- 读取结果缓存到响应对象上，错误路径要三次检查响应体（判错误类型 / 取预览 / 判可重试），**不能重新解析**。
- 解码 JSON 时必须显式使用 `response.encoding` 或 UTF-8，**不得沿用 HTTP/1.0 的 ISO-8859-1 兜底**：上游中文错误消息是本工具主要调试界面，走错解码会变乱码（旧仓专门修过）。

---

## 8. MCP stdio 接口

### 8.1 传输

换行分隔的 JSON-RPC 2.0，UTF-8，LF 结尾，一条消息一行。stdout 只允许协议消息；一切诊断走 stderr。**这是 §2.1 双二进制决定的直接依据。**

协议版本目标 `2026-07-28`（最新）；最低兼容 `2025-06-18`。须支持 `initialize` / `notifications/initialized` / `tools/list` / `tools/call` / `resources/list` / `resources/read` / `ping` / `notifications/cancelled`。

### 8.2 工具面 `[冻结]`（视频工具已随范围移除）

| 工具 | 说明 | 关键约束 |
|---|---|---|
| `generate_image` | 提交并等待有界任务 | `n` 1..4（严于 GUI 的 8）；参数与校验复用 §3/§5 |
| `edit_image` | 图生图 | 同 `n` 上限；参考图来自已导入的 asset/ref id |
| `import_reference_image` | **仅 stdio 存在**：从 `allowed_input_roots` 内的本地路径导入 | 必须做 realpath 前缀校验，禁止符号链接逃逸 |
| `get_job` | 读任务 | |
| `cancel_job` | 取消 | |

参考图导入是**二选一、按传输互斥**（旧 `server.py:295-341` 的 `if remote: … else: …`）：HTTP 侧只注册 `upload_reference_image`（base64 上传），stdio 侧只注册 `import_reference_image`（本地路径）。本项目只有 stdio，因此**只实现 `import_reference_image`**，`upload_reference_image` 不需要 —— 但 MCP 客户端若要塞外部图片，仍应支持一个 base64 入参作为 `edit_image` 的参数，避免强制用户先把图落到磁盘。

资源（`image-client://` scheme，替代旧 HTTP 的 `/api/assets`）：
```
image-client://capabilities  ·  /profiles  ·  /jobs  ·  /jobs/{job_id}  ·  /assets  ·  /assets/{asset_id}
```
返回值：`CallToolResult.content` 含 `ImageContent`（base64 + mimeType）或资源链接。

`[冻结]` **MCP 结果默认只回 `image-client://assets/{id}` 资源链接，不内联图像字节。** 依据：旧设计里那个 16 MiB 上限属于 HTTP 侧（`OPENAI_IMAGE_MCP_HTTP_MAX_RESOURCE_BYTES`，`http_server.py:484`），而 `server.py:517` 是 `if remote and max_resource_bytes is not None` —— 走 stdio 时它恒为 `None`，一张 4K PNG 会整份 base64 灌进 agent 的 stdout，把上下文挤爆。同一进程既能发链接也能读链接，没有内联的理由。保留一个 `inline_images` 显式开关给需要直接拿字节的客户端。

`[冻结]` MCP 侧必须补上旧仓缺失的一条：任务等待要有截止时间。旧 `server.py:122-152` 以 0.2 s 轮询且**无 ceiling**，而 GUI 侧有 `MAX_JOB_RUNTIME_SECONDS = 900`。

### 8.3 出网安全 `[冻结]` `web_app.py:376-411`

移除浏览器后，这一节从"防跨站"降级为"防 SSRF"，但仍必须完整保留：

- scheme ∈ {http, https}；必须有 hostname；**URL 内不得含用户名/密码**；端口合法。
- 解析 hostname（含 `getaddrinfo`），**任一解析结果非 global 即拒绝**，除非 hostname 命中显式信任列表（新实现改为配置文件项，不再是 `OPENAI_IMAGE_CLIENT_TRUSTED_UPSTREAM_HOSTS` 环境变量）。
- 地址排序：IPv4 优先，再按压缩字符串序 —— 保证行为确定。
- **DNS 重绑定防护**：解析一次并固定，实际连接使用该 IP，同时保持 TLS SNI 与证书主机名校验不变（旧 `PinnedDNSHTTPAdapter`）。旧文档记为"TOCTOU 已修复"，新实现必须等价，不得退化为"校验后再解析一次"。
- 重定向：手动跟随，≤3 跳，**每一跳重新执行上述全部校验**；跨源的跳转为 `GET` 且**不回传 Authorization**；若需回传凭据（如中转视频，已随范围移除）必须显式授权。
- 所有上游响应体经 §7.4 有界读取。

---

## 9. GUI 规格

### 9.1 视图

单窗口，左栏固定（旧 `md:w-[400px]`）+ 主区三个视图：生成 / 历史 / 设置。

必须做到（旧 UI 的实际缺陷，逐条对应验收）：

| 要求 | 旧仓缺陷 |
|---|---|
| 历史跨重启保留 | 全内存，刷新即失忆（§6.2 解决） |
| 失败可重试 / 重新提交 | 错误只进一个可被覆盖的面板，无重试按钮 |
| 结果区骨架与占位 | 图片加载前无占位，布局跳动 |
| lightbox 焦点锁定 | 旧 lightbox 是 `div`，Tab 会跑到弹窗后面 |
| 键盘直达：生成 / 切模型 / 取消 / 历史导航 | 旧 UI 全文只有 1 个 `tabindex` 与 2 个 `aria-label` |
| 流式 partial 展示 | 旧后端已有 SSE + `partial_images`，前端 1500 ms 轮询，从未消费 |
| 参考图拖放 + 原生文件对话框 + 粘贴 | 旧 blob URL 受浏览器内存限制 |
| 全分辨率查看 + 双图并排对比 | 浏览器做不到桌面级 |
| 多 profile 管理与快速切换 | 旧设置弹窗内保存路径最近两个 commit 在修 |
| 下载走 `QDesktopServices` 或原生另存为 | 旧 `<a href=服务端字符串>` 未校验 scheme，靠 CSP 兜底 |

### 9.2 任务执行模型

`[冻结]` GUI 线程不做网络。任务在 bounded worker 池（默认 2，队列 8）执行，状态经信号回主线程。取消是协作式的：轮询 `should_cancel` + `deadline_at`，睡眠以 0.5 s 为单位切片。上游 HTTP 请求进行中**不能**被中途打断（旧行为，接受此限制并在 UI 上明示"取消将在当前请求结束后生效"）。

---

## 10. 从旧仓继承的坑清单（勿重新发现）

1. `LRESULT` 在 64 位必须按有符号 64 位声明；`gdi32.CreateBitmap` 而非假定可用的高层封装；`WM_TASKBARCREATED` 后必须重注册托盘图标（explorer 重启会丢图标）。
2. 无控制台的打包构建里 stdout/stderr 为 `None`，任何写日志都会打死 handler 线程 → 表现为 `ERR_EMPTY_RESPONSE`。C++ 侧不复现（无 HTTP 服务），但 **MCP console exe 仍要保证 stderr 永远可用**。
3. `TrackPopupMenu` 前必须 `SetForegroundWindow`，否则菜单不消失/不响应。
4. 旧代码双击消息号写错过（`0x0202`）。Qt 侧不需要手写，但若保留任何原生 `WndProc`，勿重新发明。
5. 构建产物入库（旧 `web/styles.css` 是提交进 git 的 Tailwind 压缩产物），一旦漏跑构建脚本就出现"类没生效"的静默故障。QML 无此路径，但**若引入任何资源编译步骤，必须与构建同事务**。

---

## 11. 打包与体积预算

部署时按需裁剪：`platforms/qwindows.dll`、`imageformats/{qjpeg,qwebp,qsvg}.dll`、`tls/` 后端、`qml/QtQuick*` 与 `QtQuick/Controls/Basic`。

### 11.1 实测体积（2026-09-27，占位 GUI 壳 + windeployqt，Release）

| 指标 | 实测值 |
|---|---|
| 部署目录总量 | **43.3 MB / 137 个文件** |
| 整包 gzip | **17.5 MB** |
| 两个 exe 自身 | 0.04 MB（GUI） + 0.02 MB（MCP） |
| 不可约 Qt 地板 | **30.6 MB** = Qt6Gui 8.9 + Qt6Quick 6.0 + Qt6Core 5.9 + Qt6Qml 5.0 + Templates2 1.8 + Network 1.7 + Controls2Basic 1.3 |
| 其余目录 | qml 1.43 / qmltooling 0.96 / platforms 0.87 / imageformats 0.68 / tls 0.34 / generic+iconengines+networkinformation 0.24 |

**修正本节原先的 `[推算]`**：静态链接这条路**走不通**。本机安装的是 Qt 官方二进制分发，只含 shared 库；静态 Qt 需自行以 `-static` 构建整个 Qt（数小时起，且此后每次 Qt 升级都要重做）。所以 12–20 MB 那个区间作废，**实际地板就是上表那 30.6 MB**，能优化的是裁剪而非链接方式。

可裁剪项（按收益排序，均需实跑验证）：

1. `D3Dcompiler_47.dll` **4.0 MB** —— 本次未加 `--no-system-d3d-compiler`；纯 QML 应用一般不需要它。
2. `qmltooling/` **0.96 MB** —— QML 调试器组件，release 分发直接删。
3. `Qt6OpenGL.dll` **1.9 MB** —— Windows 上 RHI 默认走 D3D11；能否删取决于是否用到 OpenGL 后端，**必须实测**。

三项全去掉约 **36–37 MB 部署 / 15 MB 压缩**。

`.github/workflows/ci.yml` 的 "Package and measure bundle" 步骤用同一组 windeployqt 参数在 CI 上重算这两个数，写进 job summary，并在超过 **60 MB** 时让作业失败。所以上面三项裁剪落地时，这里应当观察到下降而不是上升；若观察到上升，说明部署目录混进了多余东西（最常见的来源是 debug 版 DLL 或 qmltooling）。

### 11.2 双二进制的体积红利（实测确认）

扫导入表：`image-client-mcp.exe` **只引用 `Qt6Core.dll`**，GUI 版引用 Core/Gui/Qml/QuickControls2（Quick 经 QML 插件运行时加载）。即 §2.1 的拆分让 MCP 服务端只需约 6 MB 运行时，而完整 GUI 是 43 MB —— 给只想在 agent 里用出图能力的用户，可以只发 MCP 那半。

UPX：旧 spec 开了 `upx=True`，且自己记录"UPX 缺失时静默失效，且常触发杀毒软件启发式告警"。**建议不开。**

---

## 12. 验收清单

### 12.1 协议行为对拍

以旧仓测试为规格来源，逐条转成 C++ 用例（不复制代码，复制断言语义）：

| 旧文件 | 行 | 覆盖内容 | 状态 |
|---|---|---|---|
| `tests/test_image_client_core.py` | 209 | 端点候选、比例/分辨率推导、有界读取 | **部分已移植**（见 12.1.1） |
| `tests/test_web_app.py` | 1204（71 例） | 校验矩阵、SSRF、multipart 字段选择、响应解析、密钥存储 | 待任务 #3/#4 |
| `tests/test_mcp_core.py` | 385 | 工具/资源面、参数 schema、结果规范化 | 待 MCP 实现 |
| `tests/test_mcp_stdio.py` | 236 | stdio 帧、协议握手 | 待 MCP 实现 |
| `tests/test_mcp_http*.py`、`_tenancy.py` | ~1400 | 多租户/HTTP 控制面 | **不移植**（范围外） |

#### 12.1.1 已移植（2026-09-27，`tests/tst_endpoints.cpp` + `tests/tst_models.cpp`）

QTest + CTest，`qt_add_executable` 经 `oic_add_test()` 注册，ctest 通过 `ENVIRONMENT PATH` 指向 Qt bin（本机 Qt 非系统安装）。**实测 62 条断言全绿**（tst_models 47 / tst_endpoints 15），MSVC `/W4` 无告警。

覆盖 §4 与 §5.2 的原有断言：

- `candidate_endpoints` 四个分支（含 `/v1` 前缀基址、整端点基址就地替换、裸主机双候选且 v1 优先、空基址返回空）。
- 模型名大小写/空白归一：`GPT-Image-2`、`gpt-image-2.5-flare`、`GPT-Image-2.5-Flare` 必须为真；`gpt-image-25`（无分隔符）、`gpt-image-3`、`dall-e-3` 必须为假。
- `is_grok_image_model`：`GROK-IMAGINE`、`grok-imagine-image-pro` 为真；缺连字符的 `grok-imagine-imagequality` 与 `grok-imagine-x` 为假。
- Grok 尺寸推导表（1:1 / 16:9 / 9:16 / 兜底），以及"推导结果恒在支持集内且恒不为 `auto`"的 7×7 网格。
- `decode_base64_limited`、有界读取、UTF-8 解码、重试分类 **未移植** —— 它们属于 §7.4/§8.3，随任务 #3 的 `core/net` 一起落地。

新增覆盖（旧测试没有，属主动补强）：

1. **1536 阈值两侧**（`1536x1536` → `1k`，`1537x1537` → `2k`）——旧测试只在单侧取值。
2. **只取长边**（`1600x400` → `2k`）。
3. **非整数比例的最近值**（`1920x880` = 2.182 落在 `2:1` 与 `19.5:9` 之间，必须选 `19.5:9`）。
4. **parseSize 的畸形输入面**：`1024`（无分隔符）、`0x100`、`-5x100`、`1024x1024x512`（Python 的 `split("x",1)` 会让右侧解析失败）、`1e3x1024`。
5. **视频模型不得被认作图像模型**（`grok-imagine-video-1.5`、`grok-imagine-video`）——视频已下范围，但若分类函数错认它们，就会被推进图像管线。

#### 12.1.2 失败路径验证状态

- **已验证**：测试二进制在失败时以非零退出码上报（实测未知函数名调用 → exit 1），这正是 ctest 判定 Failed 的机制。所以"绿"不是靠人眼看输出得来的。
- **未验证**：断言本身退化时是否会报红。曾尝试临时改错一条期望值，被权限层拦下（合理：该操作与引入真实 bug 无法区分）。**下一个改 `core/` 的任务应顺带做一次失败注入 —— 改实现而非改期望值**，例如临时把 `grokResolutionFromSize` 的 `> 1536` 写成 `>= 1536`，确认 `threshold below` 那行转红后再改回。

关键必测用例（对应真实故障）：`GPT-Image-2` 大写必须走 `image[]` 分支；`background=transparent` + `gpt-image-2` 必须 4xx 本地拒绝；HTML 错误页伪装的 base64 必须报错而非产出垃圾图；中文上游错误消息不得乱码；400 不得重发 multipart 到第二候选；非 global 解析地址必须拒绝；重定向到 127.0.0.1 必须逐跳拒绝。

### 12.2 GUI 冒烟

启动 → 配 profile → 存凭据（断言 sqlite 与任何文件里都无密钥明文）→ 文生图出图 → 图生图带 2 张参考图 → 取消 → 重启后历史仍在 → MCP 进程并发运行时不互相抢锁。

---

## 13. 决策状态

### 13.1 已定（2026-09-27）

- **MCP 返回策略** = 默认只回资源链接，保留 `inline_images` 开关（§8.2）。
- **QML 基线** = Qt Quick Controls **Basic** + 自定义深色色板。本机 Qt 同时装有 FluentWinUI3 风格，将来改风格是配置级动作，不是重写。
- **项目位置** = `D:\projects\image-client`（独立仓库；旧仓原样保留当规格参考）。
- **静态链接** 判定为不可行（§11.1），体积按 shared 计算。
- **许可证** = Apache-2.0（`LICENSE`），Qt 按 LGPLv3 动态链接、上游按 MIT 保留声明（`THIRD_PARTY_NOTICES.md`）。TLS 走 Windows Schannel，不引入 OpenSSL。
- **发布前必查一项**：`THIRD_PARTY_NOTICES.md` 中关于 `qmlcachegen` 生成物不受限的表述，我只找到了 moc/rcc/uic 的同类声明。公开第一个 release 前要么确认，要么关掉 QML AOT（CMake 一行）把问题消掉。该项在文件里以 `<!-- VERIFY ... -->` 标出。

### 13.0 已废弃的未决项

原列"是否开源"一条已由 Apache-2.0 决定关闭；"QML 视觉基线"由 Basic 关闭；"MCP 返回策略"由"默认只回资源链接"关闭。

### 13.2 仍待拍板

1. **安装包形态**：portable zip / Inno Setup / MSIX。影响能否写注册表、能否做自动更新，也决定 §11 的体积该按 43 MB 还是 15 MB 报给用户。
2. **自动更新要不要**？开源 + Windows 下这是一整个子系统；不做就现在明确不做，别留白。
3. **历史保留默认值**：500 行 / 2 GiB 是我提的起点，需确认。
4. **上游协议演进策略**：新模型、新枚举、`gpt-image-2.5-*` 之后的下一代怎么跟进（跟随 provider 文档 / 固定季度 / profile 里允许 override），否则 §5.2 会腐烂。
5. **Qt 版本**：锁 6.8.3 还是升到 6.11。倾向锁 6.8.3 直到主线完成 —— 已实测全链路可用，换版本要重跑体积与样式验证。
