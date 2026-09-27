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
- **新增（旧实现没有）**：解码后还要过 §6.4 的容器探针，认不出是图片就拒收。严格 base64 只证明"这串字符是 base64"，不证明"这是图片" —— `data:image/png;base64,<!DOCTYPE html>…` 在旧管线里能一路走到落盘，变成一个谁都打不开的 `.png`。同一个 §7.3 想防的故障，只是在更早一步拦住。

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

规则 1 的优先级高于"model 不得为空"：base 已经是完整端点时根本不需要拼 model 段，先报空模型错误等于拒掉一种合法配置。旧实现把空值检查放在前面，这里按本节编号纠正。"Gemini 必须给 model"这条要求由装配层（`prepare()`）承担，没有放松。

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

**落地记录（2026-09-27，`store/paths.cpp` + `store/assets.cpp`）**：

- `resolvePaths()` 在 `LOCALAPPDATA` 缺失或不是绝对路径时**返回错误而不是退回别处** —— 退到 temp 或 profile 根正是本节禁止的事。测试用清空环境变量的方式覆盖了这条分支。
- 配额扣减与建目录在同一个 `CreateMutexW` 名下完成；`WaitForSingleObject` 把 **`WAIT_ABANDONED_0` 视为获取成功**（前一个持有者崩了），这是选互斥量而不是锁文件的理由。跨线程争用实测：另一线程持锁 1.2 s、本线程 150 ms 预算 → 报超时且**一个字节都没落盘**。
- **ACL 实测结果修正了本节原先的假设**：本机 `%LOCALAPPDATA%` 之下新建目录的 DACL 除当前用户 / `S-1-5-18`（SYSTEM）/ `S-1-5-32-544`（Administrators）之外，还带 **`S-1-15-3-*` 应用容器 capability SID**。这类是沙箱句柄，不是"别的本地用户能读"的路径，因此策略把它们归入 `notes` 而非 `offenders`；其余任何无法识别的授权一律判失败（读不懂就当作有问题，不许蒙过去）。NULL DACL 单独判失败并在消息里说明"等于对所有账户开放"——空 DACL 不是"无权限"而是"全开放"。
- 配额拒绝发生在**建目录之前**；短写会删掉半成品（截断的图片和完整图片在文件系统上看起来一样）。
- `relPath` 删除路径先 `cleanPath` 再比较**两侧 canonical 形式**，防目录树里的重解析点把删除指向树外。
- **"只校验不修复"是错的，已改为校验→修复→再校验**（`enforcePrivateAcl`：显式写入 当前用户 / SYSTEM / Administrators 三条 `FILE_ALL_ACCESS`，并用 `PROTECTED_DACL_SECURITY_INFORMATION` 断开继承）。依据是第一次真机运行：在 `D:\tmp` 下新建的目录继承了 `S-1-5-11`(Authenticated Users) 与 `S-1-5-32-545`(BUILTIN\Users)，只校验的设计于是**拒绝保存出图结果**。老实现的毛病是"建目录不指定 mode"，正确修法是把 mode 显式设成私有，而不是发现不合规就罢工；已存在的目录同样处理（早于本策略建立的 job 目录正是可能松的那个）。

### 6.2 sqlite schema `[已实现，user_version = 1]`

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

实现要点（与草案的差别都在这里）：

- 连接按线程命名（`oic-store-<threadId>`），开 `journal_mode=WAL` + `busy_timeout=5000` + `foreign_keys=ON`。GUI 与 MCP 是两个进程共享一个文件，回滚日志模式会让第二个写者整段事务被挡；两线程各写 25 行实测互不阻塞。
- **并发首次开库（2026-09-28 修复）**：`JobManager` 的多个 worker 线程各自 open 同一个**尚不存在**的库时，会被 `database is locked` 打挂（实测 `tst_jobmanager` 30 次跑红 15 次）。根因有三层，缺一不可，全部实测钉住：
  1. `journal_mode=WAL` 的**首次**切换要抢排他锁，而 SQLite 的 busy handler **对模式切换不生效**（即使 busy_timeout=5000ms 也失败）。修法：`ensureWalMode()` 重试切换（该 PRAGMA 幂等，已 WAL 时零成本）。
  2. Qt QSQLITE 驱动下，**存活的 `QSqlQuery` 游标**会让随后的 `BEGIN IMMEDIATE` 报 `database is locked`（实测游标存活 38/8 reps 失败，`finish()` 后 0）。修法：读 `user_version` 的游标用独立作用域 + `finish()`，不让它跨进事务。
  3. `QSqlDatabase::transaction()` 发的是 `BEGIN DEFERRED`——先取读锁、写时才升级；升级时撞别的写者会**立即返回 SQLITE_BUSY 而不走 busy handler**。修法：迁移改用显式 `BEGIN IMMEDIATE`（一开始就拿写锁，能正常等待）。
  三条各自的必要性用消融实验证明：回退 WAL 重试 → 新用例红 10/15；回退游标释放 → 红 11/15；回退 IMMEDIATE → 红 20/20。回归用例 `tst_store::concurrentFirstOpenCreatesTheDatabase`（8 线程同时 open 全新库）修复前红、修复后 30/30 绿。
  遗留：`pruneToLimits` 仍用 `QSqlDatabase::transaction()`（同类 DEFERRED 隐患），但它不在首次建库路径、当前无失败用例，未在本次改动。
- `user_version` 高于本版本支持值时**拒绝打开**，不做降级迁移。
- 保留语义（`pruneToLimits`）：行预算只统计 **live 且未 pinned** 的任务，pinned 落在预算之外；软删除的行无论预算多少都被清除；字节预算按 `created_at` 从旧到新丢弃非 pinned 任务的资产，pinned 的资产即使在预算之外也不动。
- **踩过的坑，必须留在文档里**：默认构造的 `QString` 是 **null**（实测 `QString().isNull()` 为真，`QStringLiteral("").isNull()` 为假），`QSqlQuery` 把 null 绑成 SQL NULL，于是 `NOT NULL` 文本列直接拒绝插入。绑定边界统一走 `text()` 归一化为空串，"没填"在这些列上的含义就是空字符串。
- `assets.job_id` 是 `ON DELETE CASCADE`，删任务不留孤儿文件记录；插入指向不存在任务的资产会被外键挡下（测试覆盖）。

### 6.3 凭据

**[冻结] 唯一存储位置 = Windows Credential Manager。** 不再有 `secrets.json`，不再有 `chmod 600`（该调用在 NT 上只影响只读位，旧仓 `web_app.py:207` 自己注释了"尽力而为"）。

```
CredType  = CRED_TYPE_GENERIC            （SDK 里不存在 CRED_TYPE_DOMAIN_GENERIC_PASSWORD 这个名字；
                                           wincred.h:442 定义的是 CRED_TYPE_GENERIC = 1）
TargetName = "image-client/profile/<profile-name>"
CredentialBlob = UTF-8 密钥原文（无 BOM，无换行）
Persist = CRED_PERSIST_LOCAL_MACHINE     UserName = "image-client"
```
- profile 的 `credential_target` 存 TargetName；密钥永不写入 sqlite、日志、请求 URL。
- 不做文件回退。**回退等于把刚消掉的明文问题请回来。**
- 上游请求头：OpenAI/Grok 用 `Authorization: Bearer <key>`，Gemini 用 `x-goog-api-key`。
- **容量上限（实测/引头文件）**：`CRED_MAX_CREDENTIAL_BLOB_SIZE = 5*512 = 2560` 字节（`wincred.h:455`），而 §3 的 `kMaxApiKeyChars = 8192`。两者不一致，因此保存时按 2560 拒绝并说明原因，**不做截断保存**；被拒的写入不留半条凭据（测试覆盖）。真实厂商密钥在 300 字符内，这是一条边角约束而不是阻塞项。
- profile 名进入全局凭据命名空间，因此字符集收紧为 `^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$`，顺带使"名字里嵌前缀"这种自我覆盖不可能发生。
- 删除按幂等处理（不存在=成功）；读取区分"没有这条凭据"和"读失败"。
- 测试对着**真实**凭据管理器跑（mock 掉就等于没验证 §6.3）；环境拒绝写入时整类 QSKIP 而不是判失败，用例结束逐条删除，实测 `cmdkey /list` 无残留。
- "密钥不落盘"是可测的：写入哨兵密钥后扫描 `%LOCALAPPDATA%` 与临时目录下的每个文件，断言不含该字节串，同时断言 `credential_target` 确实在数据库里（否则这条断言是空的）。

### 6.4 图像解码的强制安全开关 `[冻结]`

旧仓用 Pillow 时 `Image.DecompressionBombError` 是**默认开启**的（`assets.py:176` 显式捕获）。Qt 侧对应能力**默认关闭**，必须显式设置：
```cpp
QImageReader r(...);
r.setAllocationLimit(0);          // 0 = 默认 2 GiB 上限；禁止设为 SIZE_MAX
r.setImageCountLimit(1);          // 拦 GIF/TIFF 多帧放大
```
解码仅用于取尺寸与校验"确实是图片"，结果图字节原样落盘，不做重编码。

**落点说明（2026-09-27）**：store 侧需要的只是尺寸与"是不是图"，而 `QImageReader` 会把 Qt6Gui 拖进 `image-client-mcp.exe`，抵销 §11.2 那半边的小体积。因此 `store/imageprobe.cpp` 直接解析容器头（PNG/JPEG/GIF/BMP/WebP 三种 chunk），§6.4 的开关在真正解像素的地方（GUI 显示与预览）仍然必须设置 —— 那部分随 GUI 任务落地。
**入库前必须过探针**：`normalizeReferences()`（图生图参考图）与 §7.1 的结果落盘都要求 `recognized == true`，否则"是合法 base64"会被误当成"是图片"。

探针的正确性不是自证：PNG/JPEG/BMP 用 `QImageWriter` 现场编码、再用 Qt 自己的解码器读回尺寸做对照（**同一批字节两个实现互相印证**），GIF/WebP 因本机无 WebP 解码器只能手工构造字节，文档如实记为"仅算术被覆盖"。测试刻意用非正方形尺寸（如 512x17），宽高互换才会失败。

### 6.5 本机 Qt 缺 WebP 支持 `[待拍板]`

实测：整个 `D:\tools\Qt\6.8.3\msvc2022_64` 下**没有任何 WebP 相关插件**（`plugins/imageformats` 只有 gif / ico / jpeg / svg）。后果：
- §11 部署清单里列的 `imageformats/qwebp.dll` 在本机不存在，那一行按现状是错的（§11 已按实测改为 gif/ico/jpeg/svg 四对）。
- 三家 provider 的 `output_format` 都允许 `webp`（§5.5 是"非空即透传"），选了就存回文件但 **GUI 显示不出来**。

可选路径：(a) `output_format` 白名单收到 png/jpeg（改动最小，代价是少一种格式）；(b) 自行编译 libwebp + Qt 的 qwebp 插件并随包分发（体积 +约 0.3 MB，代价是引入一个自建依赖，LGPL 义务见 §11.1）；(c) 允许写 webp 但界面明示无法预览。当前实现不替用户决定：探针识别 webp，落盘扩展名照给。

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

落地记录（`protocol/parsers.cpp::parseImageResponse`）：

- 「截断采纳」与「立即报错」是两件不同的事，实现分别为 `maxItems`（取前 N 条，成功返回）与 `totalBytes`（清空已收集图片、整体失败）。攒够一半就返回会被界面当成出图成功，这条用 `totalSizeCapAbortsTheWholeResult` 钉住。
- `url` / `result` 两条分支各自都要覆盖"下载成功→转 data URL"和"下载失败→保留原链接"两种结局（冻结的旧行为）。只测一条会让人以为另一条也成立。
- 取数走 `MediaFetcher` 注入点，与传输层解耦；`MediaLimits` 同理 —— 三条上限默认就是 §3 的冻结值，参数化只为让小用例能触发累计上限，不必造 100 MB 的 fixture。
- 诊断结构固定为 `{message, client_request_id, endpoint, raw}` 的紧凑 JSON：旧实现抛的就是这个形状，用户与 MCP 错误面都在读它。`raw` 一律过 `compactRawResponse`（>64 KiB 时丢 `data`/`b64_json`/`result` 与任何 >4 KiB 的值，并置 `_omitted_large_fields`）。

### 7.2 Gemini

遍历 `candidates[].content.parts[]`，取 `inlineData`（**兼容 `inline_data` 拼写**）且 `mimeType` 以 `image/` 开头者，有界解码 → data URL。零张图片时的错误消息固定为 `"Gemini 返回中没有可用的图片，可能被安全策略拦截或模型不支持生图"`，并附红acted 原始响应：必须把 `inlineData.data` 的值替换成 `"<omitted>"` 再进诊断，否则 32 MB 图像会进错误日志。

落地记录：脱敏要按"父键 ∈ {inlineData, inline_data} 且键名 = data"判定，所以递归必须携带父键 —— 不判断父键会把 `candidates[].finishReason` 之类无关字段一起吞掉，也漏掉别的同名 `data`。`mimeType` / `mime_type`、`inlineData` / `inline_data` 两种拼写都要收（实测响应里都有）。非 `image/` 的 inlineData 跳过而不报错，因为安全拦截场景下正文里确实只有文字 part。

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
- **Qt 侧落地形态（2026-09-27 实测，Qt 6.8.3 + Schannel + MSVC 14.51；每个用例连跑 10 次结果一致）**：`QAbstractSocket::setPeerAddress` 在 6.8.3 是 **protected**（`qabstractsocket.h:211`），应用代码取不到；`QNetworkRequest` 也没有对等的地址接口。可用的组合是**「URL 主机段写成已固定的 IP 字面量 + `QNetworkRequest::setPeerVerifyName(原主机名)` + 显式 `setRawHeader("Host", 原 authority)`」**，三件套缺一不可：
  - `setPeerVerifyName` 决定**证书主机名校验目标**：给一个不匹配的名字 → `The host name did not match any of the valid hosts for this certificate`；完全不设 → 退回按 IP 字面量比对，同样拒绝。校验没有被跳过，也没有静默降级。
  - `setPeerVerifyName` 同时决定 **SNI**：IP 字面量 URL + `peerVerifyName=pin.example.test` → ClientHello 内 `server_name=pin.example.test`。**不显式设它 SNI 就整个消失**（Qt 按 URL 主机生成，而 RFC 6066 禁止 IP 字面量作 server_name）—— 忘了设不只是主机名校验变弱，还会让 CDN／虚拟主机选不到站点。这条是抓 ClientHello 字节确认的，不是读文档得来的。
  - `Host:` 头默认按 URL 生成，即退化成 `127.0.0.1:port`，**必须显式设置**；显式值原样到端（非默认端口也保留），不被 QNAM 覆盖，也不会出现两个 `Host`。线上头名是小写 `host:`，HTTP/1.1 头名大小写不敏感，不用处理。
  - `QSslConfiguration::setCaCertificates()` 在 Schannel 后端**确实生效**：同一张自签证书，自定义 CA 列表 → 200；换成系统 297 个根 → `The certificate is self-signed, and untrusted`。§6.3 的"信任本地网关"配置项因此可做，不必写系统证书存储。
  - 强制 TLS 1.2 与强制 TLS 1.3 两条路径都通过，无协议相关差异。
  - **写测试时的坑**：上游 socket 在 `connected` 之前 `write()` 的字节会被丢掉。最初观察到的"偶发 12 秒挂起"就是它造成的，与 Qt 无关；假端点必须把字节缓存到 `connected` 之后再写。
- **必须显式关掉 HTTP/2**：`QNetworkRequest::Http2AllowedAttribute` 在 Qt 6.8 默认是允许的。2026-09-27 第一次打真实上游（new-api 网关）时，同一条 `POST /v1/images/generations` 在 h2 上拿到的是 **`200 text/html` + 8 字节 `Welcome!`**，HTTP/1.1 才拿到正确 JSON。同一 IP、同一 SNI/Host 形态、同一客户端，只切换这个属性就翻转结果 —— 不是网络抖动也不是 DNS 轮换。教训：**200 + 非 JSON 是最坏的一类失败**（状态码说成功，内容说没这回事），所以 `net/transport.cpp` 的 dial 里钉死 HTTP/1.1 并写明理由。
- QNAM 默认发送 `accept-encoding: gzip, deflate` 并**自动解压**：§7.4 的上限必须按实际读到的（解压后）字节累计，`Content-Length` 是压缩前长度，只能当快速拒绝的参考，不能当唯一依据。
- **IPv4 判定不能只靠 `QHostAddress::isGlobal()`**：实测它把 **172.16/12 与 100.64/10 视为 global**（组播 `224/4` 也不在其 IPv4 判定内）。`urlpolicy.cpp` 因此自带保留段表；IPv6 侧仍依赖 Qt 谓词加 ULA，文档段与 Teredo 未枚举 —— 这是已知缺口而非取舍。
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

**落地记录（2026-09-28，`jobs/executor.cpp` + `jobs/jobmanager.cpp`）**：

- 分两层：**纯同步 `runJob(spec, deps, cancel, error)`** 跑完一个任务的完整管线（`secret → prepare → transport → parsers → probe → store`），无线程；**`JobManager`（QObject）** 是它外面的有界池。MCP 侧（§8.2「提交并等待、有截止」）直接调 `runJob` 带 deadline、不经池；GUI 侧用 `JobManager`。
- 网络/密钥/时钟/取图/字节上限全是注入 seam（`Sender` / `SecretReader` / `Clock` / `MediaFetcher` / `MediaLimits`），沿用本仓 `Resolver` / `MediaFetcher` 的既有风格，因此整条管线可离线测；默认实现由 `makeDefaultDeps()` 用一个 `net::Transport` 接上。
- **池的形状**：`kMaxConcurrentJobs`(2) 个 `QThread::create` worker + `kMaxPendingJobs`(8) 有界待发队列 + `kMaxLiveJobs`(64) 内存活跃上限；队列/活跃满时 `submit()` **fail-fast** 返回错误（有界即有界，不阻塞调用线程）。状态经 `submitted` / `started` / `finished` 三个 **queued 信号**回 GUI 线程（`JobOutcome` 因此注册为 metatype）。
- **每 worker 无需额外 Database 管道**：`runJob` 在**调用线程内**自建 `store::Database`，连接名 `oic-store-<threadId>` 天然落在 worker 线程上；并发写者靠 WAL + `busy_timeout=5000`（§6.2）共存。
- **取消 / 超时 / 超总量一律「零落盘」**：图片先在内存聚合（上限即解析器已强制的 `kMaxGeneratedMediaBytes`），**只有整任务成功才 `AssetStore::write` + `addAsset`**；中途失败 `rollback()` 删掉已写文件。与 §7.1 `totalSizeCapAbortsTheWholeResult` 的「失败即零落盘」同侧。取消是协作式的：候选之间、Gemini n 次串行子请求之间、以及 `deadline_at = start + kMaxJobRuntimeSeconds`(900s) 处轮询；`Transport::send` 阻塞中不可打断（本节冻结限制），故「取消在当前请求结束后生效」。
- **两个时钟要分清**：job 截止（900s）走**注入 `Clock`**，jobs 层可测；单请求超时（`kDefaultTimeoutSeconds` 500s）是 `transport.cpp` 内部真实 `QTimer`，不可注入、归 `tst_transport` 测——注入假 `Clock` 验不到它。
- 端点候选循环把 §4 的分类落到 job 级：`refusesRetryAsClientError` 为真即**停、不发第二候选**（重复计费防护），5xx / upstream / 404 / 405 / html 才继续；2xx 但解析失败按硬失败处理，不再探测。
- **已知缺口（非静默丢）**：§3 的 `kMaxPartialImages` 与本节「流式 partial」暂**未实现**——当前 `transport` 读完整响应体、无 SSE 消费，故 partial 展示推迟到将来引入 SSE 传输 + GUI 任务时再做。

---

## 10. 从旧仓继承的坑清单（勿重新发现）

1. `LRESULT` 在 64 位必须按有符号 64 位声明；`gdi32.CreateBitmap` 而非假定可用的高层封装；`WM_TASKBARCREATED` 后必须重注册托盘图标（explorer 重启会丢图标）。
2. 无控制台的打包构建里 stdout/stderr 为 `None`，任何写日志都会打死 handler 线程 → 表现为 `ERR_EMPTY_RESPONSE`。C++ 侧不复现（无 HTTP 服务），但 **MCP console exe 仍要保证 stderr 永远可用**。
3. `TrackPopupMenu` 前必须 `SetForegroundWindow`，否则菜单不消失/不响应。
4. 旧代码双击消息号写错过（`0x0202`）。Qt 侧不需要手写，但若保留任何原生 `WndProc`，勿重新发明。
5. 构建产物入库（旧 `web/styles.css` 是提交进 git 的 Tailwind 压缩产物），一旦漏跑构建脚本就出现"类没生效"的静默故障。QML 无此路径，但**若引入任何资源编译步骤，必须与构建同事务**。

---

## 11. 打包与体积预算

部署时按需裁剪：`platforms/qwindows.dll`、`imageformats/{qjpeg,qgif,qico,qsvg}.dll`、`tls/` 后端、`qml/QtQuick*` 与 `QtQuick/Controls/Basic`。

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

### 11.2 双二进制的体积红利（实测确认，2026-09-27 复测）

扫 PE 导入表（手写解析，本机无 `dumpbin` 可用路径）：

| 二进制 | 引用的 Qt DLL |
|---|---|
| `image-client-mcp.exe` | **只有 `Qt6Core.dll`** |
| `ImageClient.exe` | `Qt6Core` `Qt6Gui` `Qt6Qml` `Qt6QuickControls2`（Quick 经 QML 插件运行时加载） |

`oic-core` 已经把 `Qt6::Network` 设为 PUBLIC 依赖（`urlpolicy`/`transport` 需要），但 MCP 侧当前没有任何代码引用 Network 里的符号，静态库的未引用目标文件不参与链接，其对应的 DLL 导入项因此**没有出现在最终 exe 里**。这条有代价边界：**一旦 MCP 调用 `net::Transport`，`Qt6Network.dll` 就成了 MCP 那半的硬依赖**（+1.7 MB，见 §11.1 地板表）。这是可预期的，不是回归。

复测于任务 #4 之后（2026-09-27）：`oic-core` 又增了 `Qt6::Sql` 与 `advapi32` 两个 PUBLIC 依赖，MCP 侧 exe 的导入表**仍然只有 `Qt6Core.dll`** —— 因为 MCP 的 main 目前没调用 store/secret 的任何符号。同一机制，同一结论：GUI 侧引用 Qt6Sql/advapi32 后 GUI exe 会新增这两个导入项。

即 §2.1 的拆分让 MCP 服务端只需约 6 MB 运行时，而完整 GUI 是 43 MB —— 给只想在 agent 里用出图能力的用户，可以只发 MCP 那半。

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

#### 12.1.1 已移植（2026-09-27）

覆盖文件：`tests/tst_endpoints.cpp`、`tst_models.cpp`、`tst_urlpolicy.cpp`、`tst_decode.cpp`、`tst_retrypolicy.cpp`、`tst_transport.cpp`、`tst_imageprobe.cpp`、`tst_store.cpp`、`tst_assets.cpp`、`tst_credentials.cpp`、`tst_protocol.cpp`、`tst_parsers.cpp`、`tst_jobs.cpp`、`tst_jobmanager.cpp`、`tst_jobs_integration.cpp`。

QTest + CTest，`qt_add_executable` 经 `oic_add_test()` 注册，ctest 通过 `ENVIRONMENT PATH` 指向 Qt bin（本机 Qt 非系统安装）。**实测 368 条用例全绿 / 15 个测试二进制**（endpoints 15 · models 47 · urlpolicy 45 · decode 10 · retrypolicy 24 · transport 15 · imageprobe 16 · store 15 · assets 42 · protocol 69 · parsers 24 · credentials 21 · jobs 14 · jobmanager 7 · jobs_integration 4），MSVC `/W4` 无告警。jobs 三层于 2026-09-28 落地（§9.2 落地记录）；store 于同日 +1（`concurrentFirstOpenCreatesTheDatabase`，§6.2 并发首次开库修复）。

覆盖 §4 与 §5.2 的原有断言：

- `candidate_endpoints` 四个分支（含 `/v1` 前缀基址、整端点基址就地替换、裸主机双候选且 v1 优先、空基址返回空）。
- 模型名大小写/空白归一：`GPT-Image-2`、`gpt-image-2.5-flare`、`GPT-Image-2.5-Flare` 必须为真；`gpt-image-25`（无分隔符）、`gpt-image-3`、`dall-e-3` 必须为假。
- `is_grok_image_model`：`GROK-IMAGINE`、`grok-imagine-image-pro` 为真；缺连字符的 `grok-imagine-imagequality` 与 `grok-imagine-x` 为假。
- Grok 尺寸推导表（1:1 / 16:9 / 9:16 / 兜底），以及"推导结果恒在支持集内且恒不为 `auto`"的 7×7 网格。
- 严格 base64（§7.3）与重试分类（§4）已移植（`decode.cpp` / `retrypolicy.cpp`）。
- **§7.4 有界响应体读取已随 `core/net/transport.cpp` 落地**：`Content-Length` 先做快速拒绝，实际字节按 64 KiB 分块累计并越限即 `abort()`，两条路径各自有用例（`declaredLengthBeyondCapIsRejected` / `unboundedStreamIsRejected`，后者故意不带 `Content-Length`，只有累计器能拦）。诊断解码走 `QString::fromUtf8`，不经 ISO-8859-1。**仍未移植**：把三次响应体检查复用同一份缓存（当前 `Reply::body` 就是一份缓存，调用方直接取，不需要 §7.4 那条"不能重新解析"的额外守卫）。

新增覆盖（旧测试没有，属主动补强）：

1. **1536 阈值两侧**（`1536x1536` → `1k`，`1537x1537` → `2k`）——旧测试只在单侧取值。
2. **只取长边**（`1600x400` → `2k`）。
3. **非整数比例的最近值**（`1920x880` = 2.182 落在 `2:1` 与 `19.5:9` 之间，必须选 `19.5:9`）。
4. **parseSize 的畸形输入面**：`1024`（无分隔符）、`0x100`、`-5x100`、`1024x1024x512`（Python 的 `split("x",1)` 会让右侧解析失败）、`1e3x1024`。
5. **视频模型不得被认作图像模型**（`grok-imagine-video-1.5`、`grok-imagine-video`）——视频已下范围，但若分类函数错认它们，就会被推进图像管线。

传输层（`tst_transport.cpp`，15 例）对着 127.0.0.1 上的假端点跑**真实** `Transport::send()`，只把 DNS 换成注入点，因此 §8.3 的固定与逐跳校验走的是生产同一条代码路径：

6. **三件套里 Host 那件**：端点收到的请求行必须是 `host: pin.example.test:<port>` 而不是 IP —— 这条正是失败注入 #2 钉住的性质。
- **失败注入 #3（2026-09-27，ACL 兜底）**：把 `evaluateGrants` 里「读不到任何授权条目也算失败」这半个条件去掉，`tst_assets` 恰好红 1 条（`aclPolicy(no grants)`）、其余 40 条不动。Windows 上空 DACL 的含义是**对所有账户开放**而不是「无权限」，所以这条兜底必须有用例钉住 —— 实现里太容易写成只判 `offenders.isEmpty()` 就放行。已还原复验 10/10 全绿。
- **失败注入 #4/#5/#6（2026-09-27，协议层与解析层）**：#4 把 `openAiEditFileField` 强制成 `image` —— `tst_protocol` 红 7 条（6 个字段名用例 + 那条逐字节比较的 multipart 用例），其余 62 条不动，红的正是旧 CLI 静默走错分支的那批拼写（大写、带空白、2.5 变体）。#5 关掉 Gemini 诊断脱敏 —— 恰好红 1 条（`geminiEmptyResultUsesFrozenMessage`）。#6 让超总量时返回半份图集 —— 恰好红 1 条（`totalSizeCapAbortsTheWholeResult`）。三次注入都已还原并复验 12 个二进制全绿。
7. **调用方不能伪造 Host**：`Request::headers` 里塞 `Host: evil.example.net` 必须被传输层覆盖，否则固定地址形同虚设（还能被用来做请求走私）。
8. **凭据不跨源**：302 到另一 authority 时第二个请求不得带 `Authorization`，且降级为 `GET` 并丢掉 `Content-Type`/`Content-Length`；同源 307 反过来必须保留方法、body 和凭据。链接重定向超过 `kMaxRedirects` 必须拒且实际发出的请求数恰好等于上限+1。
9. **拒绝在拨号之前**：字面量 `127.0.0.1` URL、解析到 loopback 的未信任域名、URL 内含用户名/密码三种情形，都必须在假端点的请求计数上留下 0 —— 断言"没连出去"而不是"报错了"，否则先连后拦的实现也能骗过测试。

#### 12.1.2 失败路径验证状态

- **退出码机制已验证**：测试二进制在失败条件下返回非零（实测未知函数名调用 → exit 1），ctest 据此判 Failed，所以"绿"不是人眼看输出得出的。
- **失败注入 #1（2026-09-27）**：把 `retrypolicy.cpp` 的 `status >= 500` 临时改成 `>= 600`（**改实现，不改期望**），`tst_retrypolicy` 随即报红而其余四个测试保持通过，确认断言真的在约束行为而非装饰；随后已改回并复验全绿。当初"改期望值"的做法被权限层正确拦下——那与引入真实 bug 无法区分，改实现才是对的注入点。
- **失败注入 #2（2026-09-27，传输层）**：删掉 `transport.cpp` 里显式设置 `Host:` 的那一行（即退化为 §8.3 描述的错误形态），`tst_transport` 报红 3 条、通过 12 条，红的正好是三个断言 Host 的用例（`pinnedRequestKeepsOriginalHost`、`hostHeaderFromCallerIsOverridden`、`crossAuthorityRedirectDropsCredentialsAndMethod`）。SSRF 拒绝、重定向、有界读取、重试分类全部不受影响 —— 说明这些用例彼此独立，不是"一处改动全场飘红"的那种脆弱套件。改回后 6 个测试二进制复验全绿。
- **失败注入 #7（2026-09-28，jobs 执行器）**：把 `executor.cpp` 里「客户端错误即停」的 `refusesRetryAsClientError` 分支改成永不触发 —— `tst_jobs` 恰好红 1 条（`clientErrorStopsWithoutResendingMultipart`，实测 `net->calls` 变 2、期望 1），其余 13 条不动。钉的正是 §4 的重复计费防护：400 客户端错误绝不把 multipart 重发到第二候选。已还原复验。
- **失败注入 #8（2026-09-28，JobManager 池）**：把 worker 数改成 `maxConcurrent * 3` —— `tst_jobmanager` 红 2 条（`concurrencyIsBoundedByConfig` 的 `maxActive>2`；`submitFailsFastWhenQueueFull` 的 `d.isEmpty()` 变假，因为多余 worker 把队列抽干），其余 3 条不动，证明「并发上限」与「队列满 fail-fast」两条都真在约束池大小。这次注入还**顺带炸出一个测试自身的析构顺序死锁**：`ReleaseGuard` 若声明在 `JobManager` 之前，则 manager 先析构、`thread->wait()` 撞上仍被假 sender 阻塞的 worker → 挂死（CI 会超时而非报红）。已把 guard 移到 manager 之后声明（先析构、先放行），还原注入后复验 15 个二进制全绿。**教训**：阻塞式假件 + RAII 释放时，释放者的声明顺序必须在被守护对象之后，否则一次失败会表现成超时挂起而不是可诊断的断言。

关键必测用例（对应真实故障）：`GPT-Image-2` 大写必须走 `image[]` 分支；`background=transparent` + `gpt-image-2` 必须 4xx 本地拒绝；HTML 错误页伪装的 base64 必须报错而非产出垃圾图；中文上游错误消息不得乱码；400 不得重发 multipart 到第二候选；非 global 解析地址必须拒绝；重定向到 127.0.0.1 必须逐跳拒绝。

#### 12.1.3 真机验证（2026-09-27，第一次接真实上游）

用一个 new-api 网关跑完整链路，**不经 mock**：协议判定 → 固定 IP 出网 → Schannel TLS → 响应解析 → 容器探针 → 落盘。27.5 s 返回 1.65 MB JSON，解析出 1 张 PNG（1024x1024、1234875 字节），落盘后由**无关解码器**（GDI+）独立确认宽高。

- `/v1/models` 实测只暴露 `gpt-image-2`、`gpt-image-2.5-flare`、`gpt-image-2.5-sunburst` —— §5.2 里「`gpt-image-2.5-*` 沿用 gpt-image-2 的 Images API 路径」这条与真实世界对上了。
- 响应里 `data[0]` 的键是 `b64_json` / `width` / `height` / `revised_prompt`，**没有 `output_format`**，所以 MIME 只能由 `output_format` 缺省推成 `image/png`（§7.1 的优先级写法成立）。
- 一次真机运行抓出两个本地假端点结构上不可能发现的 bug：HTTP/2 路由差异（§8.3）与继承 ACL 过松（§6.1）。本地端点永远说 HTTP/1.1、永远建在干净的目录下。**所以 §12 的真机对拍不是收尾仪式，是发现手段**：provider 行为一变就要重跑。
- 仍待真机覆盖：`url` 分支的下载回退（本次上游只回 b64_json）、Grok 与 Gemini 两条协议、图生图 multipart。

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

1. **安装包形态**（2026-09-27 细化）：要 setup 式安装是定了，**格式没定**，因为两件事被混在一起谈过：
   - **签名证书是独立的一轴**，与选哪种格式无关。Inno Setup / NSIS / MSI 不签名也能装，只是下载后首次运行会撞 SmartScreen「未知发布者」提示（点"仍要运行"即可）；MSIX 不签名则**装不上**，必须开开发者模式或让目标机器信任一张自签证书。
   - **MSIX 对本项目还有第二重代价**：App Container 会把 `%LOCALAPPDATA%` 虚拟化成包内目录（repair/卸载即清空，历史库和 asset 目录都在里面），并且包外文件系统只读 —— 用户想把图存到"下载"目录需要 `broadFileSystemAccess` 之类的可选能力。§6.1 的存储设计和 MSIX 沙箱是相互打架的。
   - **MSIX 已排除**（2026-09-27）：本软件要分发给别人用，而 MSIX 未签名装不上、签名要花钱，还要背上上面那条沙箱冲突。
   - **Inno Setup vs WiX 的分岔点只有一个**：是否需要**企业批量部署**（GPO / SCCM / Intune 派发 .msi）或 **.msp 增量补丁**。不需要 → Inno Setup：per-user 安装免管理员、单 setup.exe 双击即装、脚本短、CI 里一个 `ISCC.exe` 步骤就够。需要 → WiX（.msi 是 IT 部门的通用货币），代价是 XML 编写量与 MSI 调试（verbose log）会摊到每次改打包的日常里。两者产物内容完全相同（§11 的部署目录），换格式不动应用代码，所以这不是一个不可逆决定。
   - 零成本候选：**Inno Setup**（推荐，单 setup.exe、支持 per-user 安装免管理员、卸载器齐全）、NSIS、WiX（出 MSI，企业分发友好但 XML 更重）。三者都免费且开源。开源 + Windows 下这是一整个子系统；不做就现在明确不做，别留白。
3. **历史保留默认值**：500 行 / 2 GiB 是我提的起点，需确认。
4. **上游协议演进策略**：新模型、新枚举、`gpt-image-2.5-*` 之后的下一代怎么跟进（跟随 provider 文档 / 固定季度 / profile 里允许 override），否则 §5.2 会腐烂。
5. **WebP 要不要支持**：本机 Qt 完全没有 WebP 插件（§6.5），三选一 —— 把 `output_format` 收到 png/jpeg、自行编译 qwebp 插件随包分发、或允许落盘但界面明示无法预览。当前实现不替你决定。
6. **Qt 版本**：锁 6.8.3 还是升到 6.11。倾向锁 6.8.3 直到主线完成 —— 已实测全链路可用，换版本要重跑体积与样式验证。
