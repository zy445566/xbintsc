# 兄弟们 typescript可以编译成原生二进制了

## 缘由
2019年的时候研究过一段时间编译器，当时写了一个迷你的JS编译器，只支持function和number类型，但别小看就这两个类型，差不多花费了两周时间去实现，所以编译器是需要大量的时间和精力去做这个事情，当时就感叹可能我这辈子都不太可能能撸出一个相对完整的编译器了。

2026年9月的某个下午，群里有人聊到了前端/js/ts永远都只能在鄙视链的最底端，因为它不能像C++和rust一样生成二进制文件。

## 触发
这件事就像触发了我的底层代码一样，脑子里疯狂出现了一个念头，然后回过头去翻了翻当年写的编辑器文章，不！不对！现在可能真的有可能可以实现，然后我开始列出了我的思路，写了第一个文档 DESIGN.md，接下来打开pi，开撸！

很快不到一个小时，就出现第一个原型，我看完何其丑陋和简单，甚至不如我当年写的demo，于是我觉得自己重写把整个框架重新写一遍，若干天，总算是达到了我想要的水平，然后我开启了第二次尝试，这次我继续启动pi，这次发现虽然AI在架构上表现一般，但是架构完善后，它完成功能的速度不是一般的快，于是我开始了急速的迭代过程。

## 核心进展

* 绝大部分的js/ts语法以及node.js的主流方法(可选包)
* 支持了C++和rust扩展,用户可以自由扩展
* 实现了自举，即自己编译了自己
* 实现无GNU依赖运行

当然未实现的在这里：
[unimplemented.md](https://github.com/zy445566/xbintsc/blob/main/doc/unimplemented.md)
[node-unimplemented.md](https://github.com/zy445566/xbintsc/blob/main/doc/node-unimplemented.md)

当然对于实现二进制编译最大的优势主要是两点：
* 包体积巨小，编译后的二进制文件低至200kb，且可以直接运行，不再需要再捆绑几十MB的node.js运行时了
* 冷启动速度巨快，本机实测比node.js原生大概快了120-200倍左右

## 使用方法
在 [releases页面](https://github.com/zy445566/xbintsc/releases) 下载zst压缩包解压后，运行即可,下面以windows作为案例
```
# 编译
.\xbintsc-win32-x64\bin\xbintsc.exe build .\hello.ts --out .\build
# 运行
.\build\hello.exe
```

# 附录
Github 地址： [https://github.com/zy445566/xbintsc](https://github.com/zy445566/xbintsc)
兄弟们 有兴趣可以一起研究 `;)`








