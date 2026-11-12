# 用户注册 / 登录 / 注销（gui + IndexedDB + sessionStorage）

一个用 `gui` 扩展在原生窗口里跑起来的「表单」示例：**注册**、**登录**、**注销**。
用户数据存进 **IndexedDB**（SQLite 文件后端，重启后仍在），登录状态存进
**sessionStorage**（进程内，重启后清空）——这正是浏览器里两套存储的语义。

```
examples/gui/auth/main.ts              程序本体：开窗、loadHTML、run()
examples/gui/auth/index.html           表单界面 + 内联 <script lang="ts"> 逻辑
examples/gui/auth/xbintsc.config.json  入口 + gui 扩展 + storage 文件后端
examples/gui/auth/.data/auth.db         运行后生成的 SQLite 数据库（用户数据）
```

## 运行

```bash
npx tsx src/cli/main.ts run examples/gui/auth/main.ts
```

从本目录运行（配置文件里已声明 `entry` 和 `extensions`，所以不需要任何命令行开关）：

```bash
cd examples/gui/auth
npx tsx ../../../src/cli/main.ts run
```

发布版 xbintsc 不需要 `npx tsx` 前缀：

```bash
xbintsc run examples/gui/auth/main.ts
```

打包成独立可执行文件：

```bash
npx tsx src/cli/main.ts build examples/gui/auth/main.ts -o build/auth/auth.exe
```

窗口一直开着，直到你关掉它。设 `XT_GUI_AUTOCLOSE_MS=<毫秒>` 可让引擎到点自动关窗
（无头冒烟测试用）。Windows 上把 `xbintsc.config.json` 里的 `app.console` 留着，
stdout/stderr 才能打到控制台（GUI 程序默认是 Windows 子系统）；不需要那份输出时删掉即可。

## 怎么用

1. 输入用户名和密码，点 **注册**：写入 IndexedDB，并把用户名写进 sessionStorage，自动登录。
2. 点 **注销**：清空 sessionStorage，回到未登录界面。
3. 再输入刚才的账号点 **登录**：从 IndexedDB 读回用户，校验通过后恢复登录状态。
4. 关掉窗口重开：用户还在（IndexedDB 是文件后端），但登录状态没了（sessionStorage 是内存）。

## 关键实现

### IndexedDB（用户数据，持久化）

```ts
const openRequest = indexedDB.open("auth-demo", 1);
openRequest.onupgradeneeded = () => {
  openRequest.result.createObjectStore("users", { keyPath: "username" });
};
openRequest.onsuccess = () => {
  db = openRequest.result;
  // db.transaction("users", "readwrite").objectStore("users").put({...})
  // ...get(username) / put(...) 都在各自的 onsuccess 回调里读写 req.result
};
```

和浏览器一样，`onsuccess` / `onerror` / `onupgradeneeded` 从**微任务**里触发，所以读写
都要放进回调，不能同步 `return`。本引擎的 IndexedDB 是精简子集：有 `open`、
`createObjectStore`、`transaction`、`put`/`add`/`get`/`getAll`/`delete`/`clear`/`count`，
**没有** index、游标和 `IDBKeyRange`（见 `doc/implemented/storage.md`）。

持久化来自 `xbintsc.config.json` 里的文件后端：

```json
{ "storage": { "backend": "file", "path": ".data/auth.db" } }
```

不配这项时默认是 `memory`，用户数据也会随进程一起消失。`storage.path` 相对配置文件
所在目录解析。

### sessionStorage（登录状态，进程内）

```ts
sessionStorage.setItem("auth.currentUser", username);   // 登录 / 注册
sessionStorage.getItem("auth.currentUser");              // 判断是否已登录
sessionStorage.removeItem("auth.currentUser");           // 注销
```

它**始终**在内存里（会话不会比进程活得更久），所以天然适合「本次运行是否已登录」这类状态。

> 注意要用**裸的** `sessionStorage` / `indexedDB` 全局名。`window.localStorage` 是 gui
> 引擎自己的内存存储；裸的 `localStorage` / `sessionStorage` / `indexedDB` 才是这里说的
> 基于 SQLite 的核心全局（gui 没有 `window.sessionStorage` / `window.indexedDB`）。

### 表单

`<form>` + 两个 `type="submit"` 按钮，脚本监听一次 `submit` 事件，用 `event.submitter.id`
区分「注册」还是「登录」；在输入框里按回车是隐式提交（没有 submitter），按登录处理。

登录后给卡片加 `.logged-in` class，CSS 据此**隐藏用户名/密码输入框**，并把「注册/登录」
换成「注销」——登录后表单里只剩一个注销按钮。

密码框右侧有个「小眼睛」按钮，点击在 `type="password"` 与 `type="text"` 之间切换，
明文/掩码显示已输入的密码；切换 type 不会清空已输入的值（引擎对 text ↔ password
这类同类切换会保留 live value）。

因为 `indexedDB.open` 从微任务里 settle，注册/登录按钮一开始是 `disabled`，等 `onsuccess`
之后才启用；`dbReady` 标志同时拦住按回车触发的隐式提交，避免在数据库就绪前点太快。

## 已知取舍

- **密码不是明文落盘**：脚本用一个非加密的字符串散列（仅演示用）存摘要、比对摘要。
  真实应用应改用 `node` 扩展的 `crypto` 做加盐哈希。
- **这是演示，不是生产安全边界**：没有锁定、没有限流，任何能在本机打开窗口的人都能
  注册账号。xbintsc 的 gui 页面本身也不是浏览器沙箱——脚本就是普通原生 TS，能 `import`
  任何编译器允许的东西。

## 相关文档

- [doc/ai/extensions.md](../../../doc/ai/extensions.md) — `gui` 与 `storage`（含 IndexedDB 子集）
- [doc/gui/api.md](../../../doc/gui/api.md) — 窗口、DOM、事件与浏览器全局
- [doc/gui/dom.md](../../../doc/gui/dom.md) — 表单控件与 AOT `<script>`
- [doc/implemented/storage.md](../../../doc/implemented/storage.md) — localStorage / sessionStorage / indexedDB
