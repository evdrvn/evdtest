package.path = "../lua/?.lua"
local evdtest = require("evdtest")

function foo()
    evdtest.waitevent("foo", true)
end

function bar()
    evdtest.waitevent("bar", true, 0)
end

evdtest.startcoroutine(foo)
evdtest.startcoroutine(bar)

evdtest.waitevent("hello world")
