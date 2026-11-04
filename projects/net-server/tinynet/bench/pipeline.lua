-- wrk 脚本：每次发送 16 个连在一起的请求（HTTP 流水线）
-- 用法：wrk -s bench/pipeline.lua http://127.0.0.1:8000/
init = function(args)
   local r = {}
   for i = 1, 16 do r[i] = wrk.format(nil, "/") end
   req = table.concat(r)
end

request = function()
   return req
end
