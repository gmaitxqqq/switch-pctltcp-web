# Switch 家长控制 Web UI

手机浏览器直接管理 Switch 家长控制。Switch 上运行 `.nro` 启动 HTTP 服务（端口 8080），手机/平板打开网址即可操作，无需安装任何 App。

**版本**：v1.0.0 | **固件**：兼容 Atmosphere 22.1.0+

---

## 适用场景

> 外出时没有固定 IP，用手机浏览器快速设置。临时外出、手机热点场景下尤其方便。

---

## 安装

1. 从 [Releases](../../releases) 下载 `pctltcp-web.nro`
2. 复制到 SD 卡 `/switch/` 目录
3. Homebrew Menu 启动

---

## 使用方法

1. 启动后屏幕显示 IP 地址和 Web UI 地址
2. 手机/电脑浏览器打开 `http://<Switch-IP>:8080`
3. 即可在网页上管理家长控制

### Web UI 功能

- 查看实时计时状态（运行/暂停、剩余时间）
- 按天设置每日时间限制（周日至周六）
- 统一设置所有天相同限额
- 启动 / 暂停 / 重置计时器

---

## REST API

可用于自定义脚本或自动化：

| Method | Path | 说明 |
|--------|------|------|
| GET | `/` | Web UI 页面 |
| GET | `/api/status` | 计时器状态（JSON） |
| GET | `/api/settings` | 7 天限额设置（JSON） |
| POST | `/api/set` | 设置所有天：`{"minutes": 60}` |
| POST | `/api/set_day` | 设置某天：`{"day": 0, "minutes": 60}` |
| GET | `/api/version` | 版本号 |

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
├── config.json             # NRO 元数据
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
| **v1.0.0** | 首个正式版：移动端 Web UI、REST API、暗色主题 |
