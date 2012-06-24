Name
====

ngx_xconf - Nginx 配置增强模块

*This module is not distributed with the Nginx source.* See [the installation instructions](http://wiki.nginx.org/XconfModuleZh#Installation).

Status
======

This module is under active development and is not production ready yet(尚未用于生产环境!).

Version
=======

This document describes ngx_xconf [v0.1.3](https://github.com/kindy/xconf-nginx-module) released on 24 June 2012.

Synopsis
========

-- nginx.conf


    include_uri -I 'main' 'http://config-server-hostname/path/to/main.conf';


-- main.conf at config-server (可以是静态文件也可以是动态生成)


    worker_processes  1;

    events {
        worker_connections  1024;
    }

    http {
        include       mime.types;
        default_type  application/octet-stream;

        sendfile        on;
        keepalive_timeout  65;

        server {
            listen       80;

            index  index.html index.htm;
            error_page   500 502 503 504  /50x.html;
        }
    }


以上配置会在 nginx.conf 同目录下创建 nginx.conf.main.conf 并使用从 config-server 上获取到的 main.conf 的内容填充。
并解析配置内容(行为上类似 include)。

Description
===========

ngx_xconf 模块使得 Nginx 支持使用远程配置文件，这样，即使我们有成百上千台的 Nginx web 服务器需要管理，也不用操心配置差异和更新问题。

Directives
==========

include_uri
-----------

**syntax:** *include_uri [选项] uri*

**default:** *无*

**context:** 任意位置(几乎)

此指令使用上类似 include ，可以在任意位置插入配置。
只不过除了本地文件外，还支持 http 远程文件 (暂只支持 HTTP/1.0 亦不支持 chunck 编码)

之所以说 include_uri 几乎可以在任意位置使用，是因为有些指令块(block)跳过了标准指令处理逻辑，他们自己负责对块内的指令进行解析。

已知有 charset_map {} geo {} map {} split_clients {} types {} 。

**include 在这些指令块内的行为与标准 include 亦不完全相同**

### 参数


`uri` - 配置地址(支持变量插值，可通过 -n 关闭)

参数使用 [URI 形式](http://en.wikipedia.org/wiki/URI_scheme), 我们使用以下形式:

    <scheme name> : <hierarchical part> [ ? <query> ] [ # <fragment> ]

比如 `<http://config.abc.com:9999/ngx/all.conf?host=$hostname`>

当前可用 scheme 有 file, http

file 和 http URI 中的 scheme name 可以省略，识别(在执行变量插值后进行)规则为:

1. 如果以 `//` 开头，认为是 http
1. 如果以 `./` 或者 `/` 开头，认为是 file

在[下面](http://wiki.nginx.org/XconfModuleZh#scheme_-_http)有每种 scheme 的用法

### 选项


1. `-n` --- 不对 uri 做变量插值 (估计仅对 luai 有意义)
1. `-o $file.abc.conf` --- cachefile 名 (支持变量, 下面 cachefile 相关选项对 file 无效)
#:  如果不指定 cachefile ，默认为 "$file.l$line.conf"
1. `-O abc` --- cachefile 名 (支持变量)
#:  -o 的扩展，等同于 `-o "$file.abc.l$line.conf"` (有行号，cachefile 冲突的几率较小)
1. `-I abc` --- cachefile 名 (使用 I 仅仅因为它在 O 左边)
#:  -o 的扩展，等同于 `-o "$file.abc.conf"` (有时候，行号会变化)
1. `-K` --- 保留 (keep) 解析出错的 cachefile (默认删除) (这个删除会在所有尝试解析配置的时候，如 pre 和 after)
1. `-c 5s` --- pre-usecache 如果 cachefile 存在，是否直接使用
#:  -1 - 不直接使用 (默认)
#:  0  - 只要缓存文件存在就使用
#:  >0 - 根据 mtime 判断

**注**

1. 时长相关的值支持 Nginx 常用的 快速写法，如 `1d 2h` 表示 "1天 + 2小时" 详见 [[ConfigNotation#Time]]
1. 路径相关的值，不特别说明，都是相对 $conf_prefix 展开的

### 示例



    # 保存 http://config-server/app1.conf?host=$hostname 的内容到 "当前文件名.main.conf" 内并加载
    # 其中 $hostname 会被替换成当前机器的 hostname
    include_uri -I main '//config-server/app1.conf?host=$hostname';


    # 加载 $conf_prefix/abc.conf 文件，类似于 include abc.conf;
    include_uri './abc.conf';
    # 上面指令的其他写法: include_uri '$conf_prefix/abc.conf';
    # 上面指令的更多写法: include_uri 'file://$conf_prefix/abc.conf';


### 即将实现选项


1. `-t 4s` --- (timeout) uri 执行超时(仅 http 有效)
#:  0  - 死等 (受系统超时影响)
#:  >0 - 超时
1. `-C 5m` --- fail-usecache 如果获取配置失败(如http)，是否使用 cachefile
#:  -1 - 不使用，但保留缓存文件 (默认)
#:  -2 - 不使用，且删除缓存文件
#:  0  - 只要缓存文件存在就使用
#:  >0 - 根据 mtime 判断
1. `-m "abc.lua"` --- meta 文件位置(一般用于提供配置信息 和 变量，比如用于 uri 生成 或者 lua 指令用其生成模板之类)
#:  可指定多个
#:  path 可提供 meta namespace, 默认是 main
#:  namespace 会覆盖
#:  语法为 `-m "db:$conf_prefix/db.lua"` 命名规则: `[a-z][_a-z0-9]*`
#:  值引用方法 `${meta.db.xxx}`
#:  计划支持 lua, json 格式

### 变量插值语法

    $abc - [a-zA-Z][a-zA-Z0-9_]
    ${ab} - 如果需要变量名后接 类变量名字符 如 "${ab}c"

### 可用变量


1. $file   - 当前配置文件
1. $line   - 当前行(指令结束行，受 nginx 所限)
1. $prefix - nginx 运行 prefix(nginx 启动时候 -p 设定目录) ('/' 结尾)
1. $conf_prefix    - 配置 prefix ('/' 结尾)
1. $pid    - 当前 nginx master pid
1. $time   - 当前时间戳
1. $hostname       - 当前机器hostname

变量其他

1. 如果想插入 $ 可用 $$
1. 如果 $ 后面字符不匹配 [a-zA-Z{$] 那么 $ 及这个字符会被原样使用 "$/a" -> "$/a" <br>(这么做不安全，后续可能有更多扩展的用法，这样原始配置的行为就不可控了)
1. 如果变量名对应的值不存在，会插入空 "a${xx}b" -> "ab" 如果 xx 不存在

### scheme - http


uri 示例

1. `<http://config.abc.com:9999/ngx/all.conf`>
1. `//config.abc.com:9999/ngx/all.conf`

使用示例


    # 保存 http://config-server/app1.conf?host=$hostname 的内容到 "当前文件名.main.conf" 内并使用
    # 其中 $hostname 会被替换成当前机器的 hostname
    include_uri -I main '//config-server/app1.conf?host=$hostname';

    # 保存远程配置内容到 "$file.main.l$line.conf" 内并使用
    include_uri -O main '//config-server/app1.conf';

    # 加载远程配置前先看看 "$file.main.l$line.conf" 是否存在
    # 如果这个文件 1分钟内修改过，就不去远程抓取 而 直接使用
    # 如果文件不存在或者太旧，获取远程配置内容到 "$file.main.l$line.conf" 内并使用
    include_uri -c 1m -O main '//config-server/app1.conf';

    # 获取远程配置内容到 "$file.main.conf" 内并使用
    # 如果远程文件获取失败，但是本地有缓存，且更新时间在 1小时内就使用缓存的文件
    include_uri -C 1h -I main '//config-server/app1.conf';

    # 如果上面的指令获取到的配置解析错误，那么本地缓存文件会被自动删除
    # 可以使用 -K 保留解析出错的本地缓存文件，除了调试，一般不用吧?
    include_uri -K -o '$conf_prefix/main.conf' '//config-server/app1.conf';


### scheme - file


这个跟 include 几乎一样

file 类型基本上用不上什么选项，除了 `-n` 。但有谁会在路径里写原始的 `$` 呢。

uri 示例

1. `./abc/x.conf`           - include abc/x.conf;
1. `/abc/def/x.conf`        - include /abc/def/x.conf;
1. `file://abc/x.conf`      - include abc/x.conf;       (注意 : 后 / 的个数)
1. `file:///abc/def/x.conf` - include /abc/def/x.conf;

### scheme - lua (ing)


uri 示例

1. `lua:/abc.lua`
1. `lua:abc.lua`

lua 没有使用 http 那样复杂的模式，直接指定本地文件路径就行了。

指令会读取相应文件内容，并执行，将执行结果保存到 `-o` 等选项指定的 cachefile 中，
然后使用这些 cachefile 。如果 cachefile 解析出错会被删除，除非使用了 `-K` 选项。

### scheme - luai (ing)


luai 的意思是 lua inline

uri 示例

1. `'luai: return "listen 12;"'`

指令会把 luai: 后面的内容作为 lua 代码执行，并将结果保存到 cachefile 中(同 lua 类似)。

一般会配合 `-n` 选项使用 luai ，因为 lua 代码一般不需要插值，这些变量值可以在 lua 环境中直接获得


Known Issues
============

无

TODO
====

1. `Transfer-Encoding:chunked` 的支持
1. 支持内容转义，如 uri 转义，路径转义(比如我想把缓存文件放到统一个目录下，这样文件名最好把 / 变成 _)
## 预想语法 `abc.com/?a=${=$abc|u}` 会对 `${=` 和 `|u}` 之间的字符串做 变量插值后 再 转义，转义由 `|` `}` 之间的字符决定。不打算支持嵌套转义，预计有以下转义方法:
### u -> uri 转义
### p -> 路径中 / 到 _ 的转义


Nginx Compatibility
===================

The module is compatible with the following versions of Nginx:

开发基于 nginx 1.2.1 进行，在 linux-x64 和 mac os x 10.7.2 系统下测试通过

Code Repository
===============

The code repository of this project is hosted on github at [kindy/xconf-nginx-module](http://github.com/kindy/xconf-nginx-module).

Installation
============

1. 下载并安装 LuaJIT 2.0 (推荐) 或者 Lua 5.1 (Lua 5.2 尚不支持). LuaJIT 可以从 [the LuaJIT download page](http://luajit.org/download.html) 下载， Lua 可以在 [the standard Lua homepage](http://www.lua.org/) 找到.
1. 下载 ngx_xconf 模块 [下载页面](https://github.com/kindy/xconf-nginx-module/tags)
1. 下载 Nginx v1.2.1 [下载页面](http://nginx.org/en/download.html) [v1.2.1 链接](http://nginx.org/download/nginx-1.2.1.tar.gz)

参考步骤:


    wget 'http://nginx.org/download/nginx-1.2.1.tar.gz'
    tar -xzvf nginx-1.2.1.tar.gz
    cd nginx-1.2.1/

    # 告诉 ngx_xconf lua 路径信息.
    export LUA_LIB=/path/to/lua/lib
    export LUA_INC=/path/to/lua/include

    # 或者使用 LuaJIT.
    # export LUAJIT_LIB=/path/to/luajit/lib
    # export LUAJIT_INC=/path/to/luajit/include/luajit-2.0

    # 假设我们要把 Nginx 安装到 /opt/nginx/.
    ./configure --prefix=/opt/nginx --add-module=/path/to/xconf-nginx-module

    # 编译 -j2 后面的数字表示并行编译数量，如果不是多核 cpu，就去掉 -j 选项.
    make -j2
    # 安装.
    make install


编译选项
------------

Nginx 的 ./configure 中可以添加 `--with-cc-opt=" -DXCONF_URI_HTTP_USE_LUA_FILE=1"`，
这样对于 http 返回内容的解析就会由 nginx/conf/xconf_uri_http_parse_resp.lua 进行(默认使用嵌入的代码)。
解析规则和方法可以自己修改(每 include_uri 指令都会重新加载此文件)。

Bugs and Patches
================

Please report bugs or submit patches by:

1. Creating a ticket on the [GitHub Issue Tracker](http://github.com/kindy/xconf-nginx-module/issues)

Changes
=======

v0.1.3
------

24 Jun, 2012

* 功能:
	* -K 的支持
	* -c 的实现
* 一些bug修正

v0.1
----

23 Jun, 2012

* 功能: include_uri
	* file 类型支持
	* http 类型支持
	* 变量插值

参考代码

* weighttp
* [echosrv of dermesser](https://github.com/dermesser/echosrv)
* nginx core
* ngx_lua

v0
--

20 Jun, 2012

Test Suite
==========

进行中

The following dependencies are required to run the test suite:

* Nginx version >= 1.2.1

* Perl modules:
	* test-nginx: <http://github.com/agentzh/test-nginx>

感谢
======

* 感谢 chaoslawful (王晓哲) 给予大量精神指引和技术支持
* 感谢 Zhang "agentzh" Yichun (章亦春) 提供各种代码、测试根据和文档模版
* 感谢 一淘-数据部 团队的各位同学以 及 各种业务需求的折磨

Copyright and License
=====================

This module is licensed under the BSD license.

Copyright (C) 2012-2012, by Kindy Lin <kindy.lin at gmail.com>.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

See Also
========

* [HttpLuaModule](http://wiki.nginx.org/HttpLuaModule)
* [Introduction to ngx_lua](https://github.com/chaoslawful/lua-nginx-module/wiki/Introduction)
* [The ngx_openresty bundle](http://openresty.org)

Translations
============
* [en](http://wiki.nginx.org/XconfModule) 尚未完成

