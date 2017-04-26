package.path = "../lua/?.lua"
local evdtest = require("evdtest")

local a = 0
for i = 1, 10
    a = a + i
    evdtest.postevent(a)
end
