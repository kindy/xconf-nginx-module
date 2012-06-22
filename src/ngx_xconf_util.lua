
-- "$abc/xx${xx}y", {abc = "1", xx = "2"}
function format(fmt, ctx)
    -- return (string.gsub(fmt, "%${([^}]*)}", ctx).gsub(fmt, "%${([^}]*)}", ctx).gsub(fmt, "%$([a-zA-Z_][a-zA-Z0-9_]*)", ctx))

    ctx = ctx or var_ctx -- var_ctx 在 c 中初始化
    if not ctx then
        return error("no ctx")
    end

    local len, i = #fmt, 1
    local buf = {}
    local c

    while (i <= len) do
        local j = string.find(fmt, '$', i, true)
        if j == nil then -- 后面的字符串里没有 $ 了，都追加到 buf 里，打完收工
            table.insert(buf, string.sub(fmt, i))
            i = len
            break
        end

        if j > i then -- 把 $ 前面的字符 原样添加到 buf 里
            table.insert(buf, string.sub(fmt, i, j - 1))
        end

        c = string.sub(fmt, j + 1, j + 1) -- 看看 $ 下一个字符是什么 [${a-zA-Z] 要特殊处理

        if #c == 0 then
            return error("format invalid at [" .. j .. "], maybe '$' is at the end.")
        end

        if c == '$' then
            table.insert(buf, '$')
            i = j + 2
        elseif c == '{' then
            local key, j2 = string.match(fmt, "([^}]*)}()", j + 2)
            if key == nil then
                return error("format invalid at [" .. j .. "], no '}' found.")
            end

            -- print("got ctx[" .. key .. "] = " .. (ctx[key] or 'nil') .. "\n")
            if #key > 0 and ctx[key] ~= nil then
                table.insert(buf, ctx[key])
            end

            i = j2
        elseif string.match(c, '^[a-zA-Z]$') then
            -- 如果不能匹配， key = ""
            local key, j2 = string.match(fmt, "([a-zA-Z0-9_]*)()", j + 2)
            key = c .. key

            -- print("got ctx[" .. key .. "] = " .. (ctx[key] or 'nil') .. "\n")
            if ctx[key] ~= nil then
                table.insert(buf, ctx[key])
            end

            i = j2
        else
            -- 也许可以优化下，把所有非 $ 都弄进来 不过貌似没必要
            -- 或者直接把 i = j + 1 然后丢给下一个循环处理
            table.insert(buf, '$' .. c)
            i = j + 2
        end
    end

    return table.concat(buf, '')
end

local function test_format()
    local ok, txt
    local ctx = {
        a = 1,
        b = 2,
        c = 3,
    }

    ok, txt = pcall(format, "$", ctx)
    assert(not ok and string.match(txt, [[format invalid at %[%d+%], maybe '%$' is at the end%.$]]), "format(): last $ check")

    ok, txt = pcall(format, "${", ctx)
    assert(not ok and string.match(txt, [[format invalid at %[%d+%], no '}' found%.$]]), "format(): no } check")

    ok, txt = pcall(format, "11$a bc/xx${x}y ${}x", ctx)
    assert(ok and txt == [[111 bc/xxy x]], "format(): ok test 1")

    ok, txt = pcall(format, "$/", ctx)
    assert(ok and txt == [[$/]], "format(): ok test 2")

    ok, txt = pcall(format, "$_", ctx)
    assert(ok and txt == [[$_]], "format(): $[^a-zA-Z{$] test 3")

    ok, txt = pcall(format, "$1", ctx)
    assert(ok and txt == [[$1]], "format(): $[0-9] test 4")

    ok, txt = pcall(format, "$$", ctx)
    assert(ok and txt == [[$]], "format(): $$ test 5")

    ok, txt = pcall(format, "$$$", ctx)
    assert(not ok and string.match(txt, [[format invalid at %[%d+%], maybe '%$' is at the end%.$]]), "format(): $$ & last $ check")
    -- print(ok, txt)
end
test_format()

