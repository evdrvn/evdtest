package.path = "../lua/?.lua"
local evdtest = require("evdtest")

evdtest.waitevent("hello world")

local a = 0
for i = 1, 10 do
    a = a + i
    evdtest.postevent(a)
end
return a
