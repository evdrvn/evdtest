local evdtest = require("evdtest")
local socket = require("socket")
local udp = assert(socket.udp())

evdtest.waitevent("binded")

assert(udp:setsockname("*",0))
assert(udp:setpeername("localhost",1234))

function waitrecv1()
    evdtest.waitevent("received:1")
    assert(udp:send("2"))
end

function waitrecv2()
    evdtest.waitevent("received:2")
    assert(udp:send("3"))
end

function waitrecv3()
    evdtest.waitevent("received:3")
end

evdtest.startcoroutine(waitrecv3)
evdtest.startcoroutine(waitrecv2)
evdtest.startcoroutine(waitrecv1)

assert(udp:send("1"))

evdtest.waitevent("thread done")

