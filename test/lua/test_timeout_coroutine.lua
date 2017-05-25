package.path = "../lua/?.lua"
local evdtest = require("evdtest")

function foo()
    evdtest.waitevent("hello world", false)
end

evdtest.setdefaulttimeout(5)
evdtest.waitevent("hello world", true, 10)
evdtest.setdefaulttimeout(0)
evdtest.startcoroutine(foo)

evdtest.waitevent("is\\sdone$", false, 10)
