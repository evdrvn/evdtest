package.path = "../lua/?.lua"
local evdtest = require("evdtest")

function foo()
    evdtest.waitevent("hello world", false, 10)
end

evdtest.setdefaulttimeout(5)
evdtest.waitevent("hello world", true)
evdtest.startcoroutine(foo)
evdtest.setdefaulttimeout(0)
evdtest.waitevent("is\\sdone$", false)
