package.path = "../lua/?.lua"
local evdtest = require("evdtest")

function foo()
    evdtest.waitevent("hello world", false, 10)
end

evdtest.setdefaulttimeout(0)
evdtest.startcoroutine(foo)

evdtest.waitevent("is\\sdone$", false, 0)
