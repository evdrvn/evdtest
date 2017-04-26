package.path = "../lua/?.lua"
local evdtest = require("evdtest")

count = 0

function confirm()
    count = count + 1
    if count == 3 then evdtest.postevent("attendance check is done") end
end

function foo()
    evdtest.waitevent("hello world")
    evdtest.postevent("foo")
end

function bar()
    evdtest.waitevent("hello world")
    evdtest.postevent("bar")
end

function baz()
    evdtest.waitevent("hello world")
    evdtest.postevent("baz")
end

function listen_foo()
    evdtest.waitevent("foo")
    confirm()
end

function listen_bar()
    evdtest.waitevent("bar")
    confirm()
end

function listen_baz()
    evdtest.waitevent("baz")
    confirm()
end

evdtest.startcoroutine(listen_foo)
evdtest.startcoroutine(listen_bar)
evdtest.startcoroutine(listen_baz)

evdtest.startcoroutine(foo)
evdtest.startcoroutine(bar)
evdtest.startcoroutine(baz)

local eventname = evdtest.waitevent("is\\sdone$")
evdtest.assert(eventname == "attendance check is done", "unexpected eventname '"..eventname.."'")
