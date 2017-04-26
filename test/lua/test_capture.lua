package.path = "../lua/?.lua"
local evdtest = require("evdtest")
local event, observer

function capture_in_half()
    evdtest.waitevent("relay")
    evdtest.waitevent("relay")
    evdtest.waitevent("relay")
    evdtest.waitevent("relay")
    event, observer = evdtest.captureevent("relay")
    evdtest.postevent("half")
end

function listen_last()
    evdtest.waitevent("count=6")
    evdtest.waitevent("count=7")
    evdtest.waitevent("count=8")
    evdtest.waitevent("count=9")
    evdtest.waitevent("count=10")
    evdtest.postevent("done")
end

evdtest.waitevent("hello world")
evdtest.startcoroutine(capture_in_half)
evdtest.waitevent("half")
evdtest.startcoroutine(listen_last)
evdtest.releaseevent(observer)
evdtest.waitevent("done")

