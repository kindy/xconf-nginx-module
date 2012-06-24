/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

/*
 * lots of code from https://github.com/dermesser/echosrv
 * thanks dermesser
 */

#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"


#include "ngx_xconf_common.h"
#include "ngx_xconf_directive.h"
#include "ngx_xconf_uri_http_parse_resp.lua.h"

#ifndef XCONF_URI_HTTP_USE_LUA_FILE
#define XCONF_URI_HTTP_USE_LUA_FILE 0
#endif

#if (XCONF_URI_HTTP_USE_LUA_FILE)
static const char *xconf_uri_http_parse_resp_lua_file = "xconf_uri_http_parse_resp.lua";
#endif

static int ngx_xconf_uri_http_load_parse_resp_lua(ngx_conf_t *cf, lua_State *L);
ngx_str_t *ngx_xconf_uri_http_get(ngx_str_t *host, ngx_str_t *port, ngx_str_t *url, ngx_conf_t *cf, ngx_xconf_ctx_t *ctx);
static struct addrinfo *resolve_host(ngx_conf_t *cf, char *hostname, char *port, int use_ipv6);


char *
ngx_xconf_include_uri_http(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_xconf_ctx_t *ctx)
{
    ngx_str_t   in, hostname, port, url, *fetch_content;
    size_t      max_port_len = 5;
    u_char      c;
    size_t      i, s;

    /* 初始化 {{{ */
    in.len = ctx->noscheme_uri.len - 2;
    in.data = ctx->noscheme_uri.data + 2;

    /* 长度 +1 是因为 getaddrinfo 函数需要 \0 结尾 */
    hostname.len = 0;
    hostname.data = ngx_palloc(cf->pool, in.len + 1);
    port.len = 0;
    port.data = ngx_palloc(cf->pool, max_port_len + 1);
    /* }}} */

    /* 解析 hostname, port, url {{{ */
    i = 0;
    s = 1; /* 1->host, 2->port, 3->found '/' */
    /* localhost:82/h/git */
    while (i < in.len) {
        c = in.data[i];
        i++;

        if (c == '/') {
            s = 3;
            break;
        }

        if (s == 1) {
            if (c == ':') {
                s = 2;
            } else {
                hostname.data[hostname.len++] = c;
            }
        } else if (s == 2) {
            if (c >= '0' && c <= '9') {
                if (port.len >= max_port_len) {
                    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                            "http: port is too long.");

                    return NGX_CONF_ERROR;
                }

                port.data[port.len++] = c;
            } else {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "http: port is not valid.");

                return NGX_CONF_ERROR;
            }
        }
    }

    if (hostname.len == 0 && port.len == 0) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http: hostname and port can not both empty (%V).",
                &in);

        return NGX_CONF_ERROR;
    } else if (hostname.len == 0) {
        hostname.len = sizeof("127.0.0.1") - 1;
        ngx_memcpy(hostname.data, "127.0.0.1", hostname.len);
    } else if (port.len == 0) {
        port.len = sizeof("80") - 1;
        ngx_memcpy(port.data, "80", port.len);
    }
    hostname.data[hostname.len] = '\0';
    port.data[port.len] = '\0';

    if (s == 3 && i <= in.len) {
        url.len = in.len - i + 1;
        url.data = in.data + i - 1;
    } else {
        url.len = 1;
        url.data = ngx_palloc(cf->pool, 1);
        url.data[0] = '/';
    }

    ngx_log_error(NGX_LOG_INFO, cf->log, 0,
            "\n- - - - http - - - -\nhost: %V\nport: %V\nurl: %V\n- - - - - - - -",
            &hostname, &port, &url);
    /* }}} */


    /* FIXME fetch_fail */

    /* 获取 http 数据 {{{ */
    fetch_content = ngx_xconf_uri_http_get(&hostname, &port, &url, cf, ctx);
    if (fetch_content == NULL) {
        return NGX_XCONF_FETCH_ERROR;
    }

    ctx->fetch_content = fetch_content;

    return NGX_XCONF_FETCH_OK;
    /* }}} */
}


ngx_str_t *
ngx_xconf_uri_http_get(ngx_str_t *host, ngx_str_t *port, ngx_str_t *url, ngx_conf_t *cf, ngx_xconf_ctx_t *ctx)
{
    struct addrinfo    *saddr;
    ngx_str_t          *fetch_content;
    int                 rwlen, connfd = -1; /* -1 for close() in error label */
    size_t              buflen = 1024 * 50;
    u_char              buf[1024 * 50], *p, *last;
    lua_State          *L;
    int                 narg, rc;
    int                 lua_stack_n = 0;

    fetch_content = NULL;
    L = ctx->lua;

    /* 创建连接 {{{ */
    saddr = resolve_host(cf, (char *)host->data, (char *)port->data, 0);

    if (saddr == NGX_CONF_ERROR) {
        goto error;
    }

    connfd = socket(saddr->ai_family, saddr->ai_socktype, saddr->ai_protocol);
    dd("socket() -> %d", connfd);

    if (connfd == -1) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http: socket() error: %s.",
                strerror(errno));

        goto error;
    }

    rc = connect(connfd, saddr->ai_addr, saddr->ai_addrlen);
	if (rc == -1) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http: connect() error: %s.",
                strerror(errno));

        goto error;
	}
    /* }}} */

    /* 发请求到服务器 {{{ */
    last = buf + buflen;
    p = buf;

    /* TODO 请求的生成，也放到 lua 里去 */
    p = ngx_slprintf(p, last,
            "GET %V HTTP/1.0\r\nHost: %V\r\nUser-Agent: ngx-xconf\r\n\r\n",
            url, host);

    rwlen = p - buf;
    rc = ngx_write_fd(connfd, buf, rwlen);
    dd("write(%d) -> %d", connfd, rc);
    if (rc == -1) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http: write() to server error: %s.",
                strerror(errno));

        goto error;
    }
    /* }}} */

    /* 数据处理 函数 */
    if (ngx_xconf_uri_http_load_parse_resp_lua(cf, L) != NGX_OK) {
        goto error;
    }
    lua_stack_n = 1;
    narg = 0;
    lua_createtable(L, /* narr */2, /* nrec*/ 0); /* body{}, func */
    lua_stack_n = 2;

    while ((rwlen = read(connfd, buf, buflen))) {
        if (rwlen == -1) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "http get: read from server error.");

            goto error;
        }

        /* 获取到的数据交由 lua 处理，更加灵活和简单，c 处理字符串太复杂 */
        lua_pushnumber(L, ++narg);              /* n, body{}, func */
        lua_pushlstring(L, (char *)buf, rwlen); /* str, n, body{}, func */
        lua_settable(L, -3);                    /* body{str}, func */

        ngx_log_error(NGX_LOG_INFO, cf->log, 0,
                "\n- - - - http get - - - -\n%*s\n- - - - - - - -",
                rwlen, buf);
    }

    if (narg == 0) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "http get: server return empty.");

        goto error;
    } else {
        if (ngx_xconf_util_lua_pcall(cf, L, 1, 1, 0, 0) != NGX_OK) {
            lua_stack_n = 0;
            goto error;
        }
        /* lua stack: result{} */
        /*
         * result{}
         *  status_code
         *  body (trim \r)
         */
        if (! lua_istable(L, -1)) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "http get proc resp lua: lua should return a table.");

            lua_stack_n = 1;
            goto error;
        } else {
            int         status_code;
            ngx_str_t   status_txt;
            u_char     *p;

            /* ret{} */
            lua_getfield(L, -1, "status_txt"); /* status_txt, ret{} */
            lua_getfield(L, -2, "status_code"); /* status_code, status_txt, ret{} */
            lua_getfield(L, -3, "body"); /* body, status_code, status_txt, ret{} */
            lua_stack_n = 4;

            if (! (lua_isnumber(L, -2) && lua_isstring(L, -1))) {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "http get proc resp lua: lua return must have status_code(number) & body(string).");

                goto error;
            }

            status_code = lua_tonumber(L, -2);

            if (status_code != 200) {
                ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                        "http get process resp: get from \"%V\" got status code [%d], 200 needed.",
                        &ctx->noscheme_uri, status_code);

                goto error;
            }

            fetch_content = ngx_palloc(cf->pool, sizeof(ngx_str_t));
            if (fetch_content == NULL) {
                goto error;
            }

            p = (u_char *)lua_tolstring(L, -1, &fetch_content->len);
            fetch_content->data = ngx_palloc(cf->pool, fetch_content->len);

            if (fetch_content->data == NULL) {
                fetch_content = NULL;
                goto error;
            }

            ngx_memcpy(fetch_content->data, p, fetch_content->len);

            status_txt.data = (u_char *)lua_tolstring(L, -1, &status_txt.len);

            ngx_log_error(NGX_LOG_INFO, cf->log, 0,
                    "\n- - - - http get lua return - - - -\nstatus_code: %d\nstatus_txt: %V\nbody: %V\n- - - - - - - -",
                    status_code, &status_txt, fetch_content);

            goto done;
        }
    }


done:
    lua_pop(L, lua_stack_n);
    close(connfd);

    return fetch_content;

error:
    lua_pop(L, lua_stack_n);
    close(connfd);

    return NULL;
}


static int
ngx_xconf_uri_http_load_parse_resp_lua(ngx_conf_t *cf, lua_State *L)
{
    int     rc;
#if (XCONF_URI_HTTP_USE_LUA_FILE)
    size_t  len;
    char    *lua_filename;

    /* $conf_prefix/$filename\0 */
    len = cf->cycle->conf_prefix.len
            + strlen(xconf_uri_http_parse_resp_lua_file) + 1;

    lua_filename = ngx_palloc(cf->pool, len);
    ngx_memcpy(lua_filename,
            cf->cycle->conf_prefix.data, cf->cycle->conf_prefix.len);
    ngx_cpystrn((u_char *) lua_filename + cf->cycle->conf_prefix.len,
            (u_char *) xconf_uri_http_parse_resp_lua_file,
            strlen(xconf_uri_http_parse_resp_lua_file) + 1);

    rc = luaL_loadfile(L, lua_filename);
    ngx_pfree(cf->pool, lua_filename);
#else
    rc = luaL_loadbuffer(L, (const char*) xconf_uri_http_parse_resp_lua, sizeof(xconf_uri_http_parse_resp_lua), "xconf_uri_http_parse_resp.lua(embed)");
#endif

    if (rc != 0) {
        ngx_str_t   msg;

        msg.data = (u_char *) lua_tolstring(L, -1, &msg.len);

        if (msg.data == NULL) {
            msg.data = (u_char *) "unknown reason";
            msg.len = sizeof("unknown reason") - 1;
        }

        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "xconf load lua code error: %V.",
                &msg);

        lua_pop(L, 1);

        return NGX_ERROR;
    }

    return NGX_OK;
}


/* code from weighttp */
static struct addrinfo *
resolve_host(ngx_conf_t *cf, char *hostname, char *port, int use_ipv6)
{
    int err;
    struct addrinfo hints, *res, *res_first, *res_last;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    err = getaddrinfo(hostname, port, &hints, &res_first);

    if (err) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "could not resolve hostname: %s:%s, because %s",
                hostname, port, gai_strerror(err));

        return NGX_CONF_ERROR;
    }

    /* search for an ipv4 address, no ipv6 yet */
    res_last = NULL;
    for (res = res_first; res != NULL; res = res->ai_next) {
        if (res->ai_family == AF_INET && !use_ipv6)
            break;
        else if (res->ai_family == AF_INET6 && use_ipv6)
            break;

        res_last = res;
    }

    if (! res) {
        freeaddrinfo(res_first);

        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "could not resolve hostname: %s:%s, because %s",
                hostname, port, gai_strerror(err));

        return NGX_CONF_ERROR;
    }

    if (res != res_first) {
        /* unlink from list and free rest */
        res_last->ai_next = res->ai_next;
        freeaddrinfo(res_first);
        res->ai_next = NULL;
    }

    return res;
}

