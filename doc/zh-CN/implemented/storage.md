# Storage（localStorage / sessionStorage / indexedDB）

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/storage.md)

三个浏览器存储全局变量 —— `localStorage`、`sessionStorage` 与 `indexedDB` —— **始终可用**
（它们是核心全局），由内置的 SQLite（`vendor/sqlite3`，由 `npm run vendor` 获取）支撑。
无需任何扩展开关。

## 后端选择

后端在编译期由 `xbintsc.config.json` 里的 `storage` 对象决定，默认是**内存** SQLite 数据库：

```json
{ "storage": { "backend": "memory" } }
```

```json
{ "storage": { "backend": "file", "path": "data/app.db" } }
```

`storage.path` 相对配置文件所在目录解析（绝对路径原样使用）。`sessionStorage` **始终**在内存中，
因为会话不会比进程活得更久；`localStorage` 与 `indexedDB` 遵循配置的后端。驱动程序把解析好的后端与
路径嵌入为一个小对象，运行时在第一次使用时读取（`src/driver/storage.ts`）。

## 它是一个链接特性

SQLite 很重，因此整个实现是一个名为 `storage` 的**链接特性**，由始终开启的核心扩展声明：
驱动程序扫描打包后的程序，只有当代码可能触达这三个全局变量之一时才链接它。从不使用存储的程序
不会为 SQLite 付出任何体积。`--feature storage` 强制开启后端（用于扫描无法看见的存储访问），
`--no-feature storage` 则关闭它。

## 接口

| 全局变量 | 说明 |
| --- | --- |
| `localStorage` | `getItem` / `setItem` / `removeItem` / `clear` / `key(i)` / `length`，由配置的后端支撑。 |
| `sessionStorage` | 同样的接口，始终在内存中。 |
| `indexedDB` | `open(name, version?)`、`deleteDatabase(name)`；数据库支持 `createObjectStore` / `deleteObjectStore` / `transaction`；对象仓库支持 `put` / `add` / `get` / `getAll` / `getKey` / `delete` / `clear` / `count`；请求支持 `onsuccess` / `onerror` / `onupgradeneeded` / `result` / `error`。 |

indexedDB 请求从微任务中 settle，因此 `onsuccess` / `onerror` / `onupgradeneeded` 处理器像浏览器一样
异步触发。值以 JSON 序列化；键会被规范化，使数字键 `5` 与字符串键 `"5"` 保持区分。`createObjectStore`
支持 `keyPath` 与 `autoIncrement`。

## 限制

- indexedDB 子集是精简的：没有 `index`、`IDBKeyRange`、游标、`createIndex`，也没有
  `count(key)` / `getAll(query, count)` 过滤；`getAll` 返回整个仓库，`count` 统计整个仓库。
- 事务不是原子的 —— 每个对象仓库操作都会立即提交 —— 且未实现 `abort`。
- JSON 序列化意味着非 JSON 值（`undefined`、函数、`Date` 身份）会以 `null` 往返或被丢弃。
- `key(i)` / `objectStoreNames` 返回的键按插入顺序（`rowid`）；重复设置同一个 Web Storage 键会保持其原始位置。
- `gui` 扩展仍把 `window.localStorage` 作为引擎自己的内存存储；裸的 `localStorage` 全局（以及
  `sessionStorage` / `indexedDB`）则是这里描述的基于 SQLite 的全局。
