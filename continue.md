# 工作记录 — 2026-05-22 / 2026-05-26

## 本次完成内容

### 1. 项目全景梳理
- Claude 全面搜索了本仓库的 SVAC/GB35114/RTP/WebAPI 修改内容
- 关键文件路径、架构关系均已理清

### 2. 项目文档生成
- 创建了 `CLAUDE.md`，包含项目身份、所有修改点、架构说明和待办事项
- 创建了 Memory 文件（跨会话持久化），下次打开 Claude Code 会自动加载上下文：
  - `project_overview.md` — 项目概览
  - `user_profile.md` — 用户角色
  - `svac_codec.md` — SVAC 编解码集成细节
  - `gb35114.md` — GB35114 流程与状态
  - `webapi.md` — WebAPI 接口清单

### 3. 汇报文字润色
- 为 5 个 SVAC 新增接口编写了 ZLMediaKit 原版风格的接口文档
- 包含参数表、功能说明、使用示例

---

## 下次要继续的工作

### 优先事项

1. **GB35114 验签功能实现**
   - 文件：`src/Rtp/GB35114Process.cpp` — `verifyVideoFrame()` 函数（第 240 行）
   - 当前状态：直接 `return true`，验签逻辑已注释
   - 需要：加载 X509 证书 → 提取公钥 → 对帧数据验签
   - 配置项：`rtp_proxy.cert_file_path`（在 `src/Common/config.cpp` 已定义，无默认值）
   - 前置条件：解决 OpenSSL 头文件缺失问题（`openssl/bio.h` 等找不到）

2. **SVAC 解码器插件注册**
   - 文件：`src/Factory.cpp` 第 36 行
   - 当前状态：`REGISTER_CODEC(svac_plugin)` 被注释掉
   - 第三方 SDK：`3rdpart/ZXSvacDec/`（含 DLL）

3. **`startSVACRecord` 接口完善**
   - 文件：`server/WebApi.cpp` 第 2082 行
   - 当前状态：只有参数校验骨架，业务逻辑未实现
   - 与 `recordSVAC` 的区别需明确

### 已知问题

- OpenSSL 头文件未配置到 VS 编译环境，GB35114Process 编译会报 `fatal error C1083`
- Memory 目录和 CLAUDE.md 均已就绪，下次直接继续开发即可

---

## 项目环境速查

| 项 | 值 |
|----|-----|
| 项目 | ZLMediaKit SVAC Fork |
| 语言/构建 | C++11 / CMake / Visual Studio |
| 平台 | Windows (MSVC) |
| 关键宏 | `ENABLE_RTPPROXY`、`_WIN32` |
| 第三方库 | ZLToolKit, jsoncpp, media-server, ZXSvacDec |
| 配置文件 | `conf/config.ini` |
| Swagger | `www/swagger/openapi.json` |
