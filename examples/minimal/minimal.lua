local socket = require("socket")
local udp = assert(socket.udp())

assert(udp:setsockname("*",0))
assert(udp:setpeername("localhost",1234))

assert(udp:send("1"))
assert(udp:send("2"))
assert(udp:send("3"))
