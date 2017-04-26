local evdtest = {}
evdtest.observers = {}

function __evdtest_checkdone()
    for key, value in pairs(evdtest.observers) do
        if value.state == "wait" then
            evdtest.assert(value.ignore_cancel == true, "observer '"..value.eventname.."' has been canceled at "..value.submitter, true) 
        end
    end
end

evdtest.setdefaulttimeout = function(timeout)
    evdtestc.setdefaulttimeout(timeout)
end

evdtest.postevent = function (eventname)
    evdtestc.evdtest_postevent(eventname)
end

evdtest.assert = function (v, message, without_traceback)
    local message = message or ""
    local without_traceback = without_traceback or false 
    local m = "assertion failed !!: "..message
    if without_traceback ~= true then m = m.."\n"..debug.traceback() end
    assert(v, m) 
end

evdtest.startcoroutine = function (f)
    local thread = coroutine.create(f)
    local noerr, ret = coroutine.resume(thread)
    if noerr == false then error(ret) end
    return thread
end

evdtest.waitevent = function (eventname, ignore_cancel, timeout)
    return evdtest.__waitevent(eventname, false, ignore_cancel, timeout)
end

evdtest.captureevent = function (eventname)
    return evdtest.__waitevent(eventname, true)
end

evdtest.__waitevent = function (eventname, capture, ignore_cancel, timeout)
    local capture = capture or false 
    local ignore_cancel = ignore_cancel or false 
    local timeout = timeout or -1
    local thread, ismainthread = coroutine.running()
    local callstack = debug.traceback(thread, "", 2)
    local i, j = string.find(callstack, "\t")
    local submitter = string.sub(callstack, i + 1)
    local ret, observer = evdtestc.evdtest_addobserver(eventname, submitter, capture, timeout)
    evdtest.assert(ret ~= evdtestc.EVDTEST_ERROR_COMP_REGEX, "fail to compile regex pattern '"..ret.."'")
    evdtest.assert(ret == evdtestc.EVDTEST_ERROR_NONE, "fail to addobserver, ret = "..ret)

    local tbl = {}
    tbl.observer = observer
    tbl.state = "wait" 
    tbl.thread = thread
    tbl.ismainthread = ismainthread
    tbl.submitter = submitter
    tbl.eventname = eventname 
    tbl.ignore_cancel = ignore_cancel 
    table.insert(evdtest.observers, tbl)
   
    repeat
        local ret = evdtestc.evdtest_observer_trywait(observer)
        if ret == evdtestc.EVDTEST_ERROR_NONE then 
            if capture then
                tbl.state = "captured"
                return evdtestc.evdtest_observer_geteventname(observer), tbl
            else
                tbl.state = "done"
                return evdtestc.evdtest_observer_geteventname(observer), tbl
            end
        end

        if  capture == false and ignore_cancel == false then 
            evdtest.assert(ret ~= evdtestc.EVDTEST_ERROR_CANCELED, "observer '"..eventname.."' has been canceled !!")    
            evdtest.assert(ret ~= evdtestc.EVDTEST_ERROR_TIMEOUT, "observer '"..eventname.."' timeout !!")    
        end

        if capture == false and (ret == evdtestc.EVDTEST_ERROR_CANCELED or ret == evdtestc.EVDTEST_ERROR_TIMEOUT) then 
            ret = evdtestc.EVDTEST_ERROR_NONE 
            return nil
        end

        evdtest.assert(ret == evdtestc.EVDTEST_ERROR_NOT_DONE, "fali to wait observer is done, ret = "..ret)    

        if tbl.ismainthread then
            evdtestc.evdtest_wait(false)
            for key, value in pairs(evdtest.observers) do
                if value.state == "wait" and coroutine.status(value.thread) == "suspended" then 
                    local noerr, ret = coroutine.resume(value.thread)
                    if noerr == false then error(ret) end
                end
            end
            for key, value in pairs(evdtest.observers) do
                if value.state == "done" then
                    table.remove(evdtest.observers, key)
                    evdtestc.evdtest_observer_destroy(value.observer)
                end
            end

        else
            coroutine.yield()
        end
    until false
end

evdtest.releaseevent = function (observer)
    evdtest.assert(type(observer) == "table")
    for key, value in pairs(evdtest.observers) do
        if value == observer and value.state == "captured" then 
            evdtestc.evdtest_observer_releaseevent(value.observer)
            value.state = "done"
        end
    end
end

return evdtest

