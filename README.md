# Switch 家长控制 Web UI

手机浏览器直接管理 Switch 家长控制。Switch 上运行 `.nro` 启动 HTTP 服务（端口 8080），手机/平板打开网址即可操作，无需安装任何 App。

**版本**：v1.3.0 | **固件**：兼容 Atmosphere 22.1.0+

---

## 适用场景

> 外出时没有固定 IP，用手机浏览器快速设置。临时外出、手机热点场景下尤其方便。

---

## 安装

1. 从 [Releases](../../releases) 下载 `pctltcp-web-release.zip`
2. 解压得到 `pctltcp-web.nro`，复制到 SD 卡 `/switch/` 目录
3. Homebrew Menu 启动

---

## 使用方法

1. 启动后屏幕显示 IP 地址和 Web UI 地址
2. 手机/电脑浏览器打开 `http://<Switch-IP>:8080`
3. 即可在网页上管理家长控制

### Web UI 功能

- 查看今日已玩时间、剩余时间、当日限额
- **累加设置**：输入想增加的分钟数，自动叠加到当前限额（+15/+30/+60/+90 快捷按钮）
- 输入 0 可解除当日限制（设为无限）
- 每 30 秒自动刷新状态

---

## REST API

可用于自定义脚本或自动化：

| Method | Path | 说明 |
|--------|------|------|
| GET | `/` | Web UI 页面 |
| GET | `/api/status` | 当前状态（JSON） |
| POST | `/api/allow` | 累加今日限额：`minutes=N`（0=解除限制） |

### `/api/status` 响应示例

```json
{
  "daily_limit_min": 60,
  "remaining_min": 45,
  "played_min": 15,
  "today": 6,
  "today_name": "Sat",
  "version": "v1.3"
}
```

### `/api/allow` 请求

```
POST /api/allow
Content-Type: application/x-www-form-urlencoded

minutes=30
```

- `minutes=N`：在当前限额基础上增加 N 分钟（上限 1440）
- `minutes=0`：解除当日限制（设为无限）

---

## 项目结构

```
switch-pctltcp-web/
├── source/
│   ├── main.c              # 控制台 UI + 主循环
│   ├── http_server.c/h     # HTTP 服务端 + 嵌入式 Web UI
│   └── pctl_handler.c/h    # pctl IPC 封装
├── pctltcp-web.icon        # NRO 图标
├── pctltcp-web.jpg         # NRO 图标源图
├── pctltcp-web.json        # NRO 元数据
└── Makefile
```

---

## 从源码编译

```bash
export DEVKITPRO=/opt/devkitpro
make
```

推送至 GitHub 后 Actions 自动构建。

---

## 同系列工具

| 项目 | 类型 | 适用场景 |
|------|------|---------|
| [switch-parental-timer](https://github.com/gmaitxqqq/switch-parental-timer) | 本机 NRO | 在 Switch 上直接操作，无需网络 |
| [switch-pctltcp-nro](https://github.com/gmaitxqqq/switch-pctltcp-nro) | 前台 NRO + TCP | 固定 IP 局域网，PC 客户端远程管理 |
| **switch-pctltcp-web**（本仓库） | 前台 NRO + Web UI | 外出时手机浏览器管理（无固定 IP） |
| [switch-pctltcp-sysmodule](https://github.com/gmaitxqqq/switch-pctltcp-sysmodule) | 后台 sysmodule | 固定 IP 家庭环境，开机自动运行 |

---

## 版本历史

| 版本 | 变更 |
|------|------|
| **v1.3.0** | 累加模式设置（而非重置）；只设当天而非全部7天；修复计时器耗尽后溢出；修复星期读取 |
| **v1.0.0** | 首个正式版：移动端 Web UI、暗色主题 |
