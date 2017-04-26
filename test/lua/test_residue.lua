package.path = "../lua/?.lua"
local evdtest = require("evdtest")

function foo()
    evdtest.waitevent("foo")
end

function bar()
    evdtest.waitevent("bar")
end

evdtest.startcoroutine(foo)
evdtest.startcoroutine(bar)

evdtest.waitevent("hello world")
