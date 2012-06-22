local args = {...}
local body = args[1]

if not body then
    return nil
end

local ret = {}

local txt = table.concat(body, '')

ret.error = 0

local status_line = {string.match(txt, "^%s*HTTP/([0-9.]+) ([1-9][0-9][0-9])([^\r\n]*)()([\r\n]+)()")}

if status_line ~= nil then
    ret.status_code = tonumber(status_line[2])
    ret.status_txt = status_line[3]

    local pos, pos2 = status_line[4], status_line[6]
    local newline_char = status_line[5]
    if #newline_char > 2 then
        ret.body = string.sub(txt, pos2 + 1)
    else
        local xx = string.rep(newline_char, 2)
        pos2 = string.find(txt, xx, 1, true)
        if pos2 == nil then
            ret.body = ""
        else
            ret.body = string.sub(txt, pos2 + #xx)
        end
    end
else
    ret.error = 1
    ret.status_code = -1
    ret.status_txt = ""
    ret.body = txt
end


return ret

