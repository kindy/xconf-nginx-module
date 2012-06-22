#!/bin/bash

# this file is mostly meant to be used by the author himself.

version="1.2.1"
opts=$1

root=$(cd "$(dirname $0)/../" && echo $PWD)
mkdir -p $root/{build,work}

cd $root

util/bin2c src/ngx_xconf_uri_http_parse_resp.lua xconf_uri_http_parse_resp_lua >src/ngx_xconf_uri_http_parse_resp.lua.h
if [ $? != 0 ]; then
    echo 'Failed to generate the lua code (src/ngx_xconf_uri_http_parse_resp.lua).' 1>&2
    exit 1;
fi
util/bin2c src/ngx_xconf_util.lua xconf_util_lua >src/ngx_xconf_util.lua.h
if [ $? != 0 ]; then
    echo 'Failed to generate the lua code (src/ngx_xconf_util.lua).' 1>&2
    exit 1;
fi

cd $root/build

if [ ! -d nginx-$version ]; then
    if [ ! -s nginx-$version.tar.gz ]; then
        curl -L "http://nginx.org/download/nginx-$version.tar.gz" -O nginx-$version.tar.gz
    fi
    tar -xzvf nginx-$version.tar.gz
fi

cd nginx-$version/

if [[ "$BUILD_CLEAN" -eq 1 || ! -f Makefile || "$root/config" -nt Makefile || "$0" -nt Makefile ]]; then
    # LUAJIT_INC=/opt/openresty/luajit/include/luajit-2.0/ LUAJIT_LIB=/opt/openresty/luajit/lib/ \
    LUA_INC=/opt/openresty/lua/include/ LUA_LIB=/opt/openresty/lua/lib/ \
    ./configure --prefix=$root/work \
                --add-module=$root \
                --with-cc=/usr/bin/gcc-4.2 \
                --with-cc-opt=" -O0 -DDDEBUG -DXCONF_URI_HTTP_USE_LUA_FILE=1" \
                $opts \
                --with-debug
fi

if [ -f $root/work/sbin/nginx ]; then
    rm -f $root/work/sbin/nginx
fi

if [ -f $root/work/logs/nginx.pid ]; then
    kill `cat $root/work/logs/nginx.pid`
fi

make -j2
make install

rm -f $root/work/conf/xconf_uri_http_parse_resp.lua && ln -s $root/src/ngx_xconf_uri_http_parse_resp.lua $root/work/conf/xconf_uri_http_parse_resp.lua
if [ $? != 0 ]; then
    echo 'Failed to copy lua code to nginx conf directory.' 1>&2
    exit 1;
fi


echo done
