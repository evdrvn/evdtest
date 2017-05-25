#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "evdtest.h"

#define EVDTEST_EVENT_NOT_YET_HANDLING (INT_MIN)
#define EVDTEST_EVENT_TO_BE_DESTROYED  (-1)
#define EVDTEST_USLEEP_INTERVAL (10 * 1000)

extern int luaopen_evdtestc(lua_State* L);

static evdtest_error_t evdtest_event_init(
        evdsptc_event_t** event,
        evdsptc_handler_t eventhandler,
        const char* eventname,
        bool auto_destruct,
        evdsptc_event_destructor_t event_destructor);

static void evdtest_event_free(evdsptc_event_t* event);
static bool evdtest_event_addobserver(evdsptc_event_t* event);

evdtest_context_t context;

evdtest_context_t* evdtest_getcontext(void){
    return &context;    
}

static bool evdtest_thread_is_handler(pthread_t thread){
    return pthread_equal(thread, ((evdsptc_getthreads(&context.evdsptc_ctx))[0]));
}

static bool evdtest_thread_is_lua(pthread_t thread){
    return  pthread_equal(thread, context.lua_thread);
}

static bool evdtest_eventhandler_error(evdsptc_event_t* event){
   (void)event;
   return true;
}

static evdtest_error_t evdtest_postevent_error(char* format, ... ){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    evdsptc_event_t* pevent = NULL;
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    va_start(list, format);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);
 
    ret = evdtest_event_init(&pevent, evdtest_eventhandler_error, str, true, evdtest_event_free);
    if(EVDTEST_ERROR_NONE != ret) goto DONE; 
    if(evdsptc_post(&context.evdsptc_ctx, pevent)) ret = EVDTEST_ERROR_EVDSPTC_FAIL_POST;

DONE:
    return ret;
}

static evdtest_error_t evdtest_postevent_nop(char* format, ... ){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    evdsptc_event_t* pevent = NULL;
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    va_start(list, format);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);
 
    ret = evdtest_event_init(&pevent, NULL, str, true, evdtest_event_free);
    if(EVDTEST_ERROR_NONE != ret) goto DONE; 
    if(evdsptc_post(&context.evdsptc_ctx, pevent)) ret = EVDTEST_ERROR_EVDSPTC_FAIL_POST;

DONE:
    return ret;
}

static evdtest_error_t evdtest_lua_handle_error(int lua_ret){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    const char* error;

    if(lua_ret == 0){
        ret = EVDTEST_ERROR_NONE;
        goto DONE;
    }

    error = lua_tostring(context.L, -1);
    if(error == NULL) error = "";

    if(lua_ret == LUA_ERRFILE){
        ret = EVDTEST_ERROR_NOT_FOUND;
        evdtest_postevent_error("lua test script '%s' not found !!", getenv(EVDTEST_ENV_TEST_CASE));
    } else if(lua_ret == LUA_ERRSYNTAX){
        ret = EVDTEST_ERROR_BAD_LUA_SCRIPT;
        evdtest_postevent_error("lua test script error !!\n%s", error);
    } else {
        ret = EVDTEST_ERROR_ANY_LUA_EXCEPTIONS;
        evdtest_postevent_error("lua error(%d) detected !!\n%s", lua_ret, error);
    }

DONE:
    return ret;
}

static void* evdtest_thread_lua(void* arg){
    (void)arg;
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    int lua_ret = 0;

    evdtest_postevent_nop(context.start_event_name);
    if(0 != (lua_ret = lua_pcall(context.L, 0, 0, 0))){
        context.state = EVDTEST_STATUS_DESTROYING;
        evdtest_wait(true);
        ret = evdtest_lua_handle_error(lua_ret);
    }else{
        lua_getglobal(context.L, "__evdtest_checkdone");
        if(0 != (lua_ret = lua_pcall(context.L, 0, 0, 0))){
            ret = evdtest_lua_handle_error(lua_ret);
        }
        evdtest_wait(true);
    }
    context.first_lua_suspended = true;
    TRACE("lua thread done, ret = %d", ret);
    __sync_synchronize();

    if(ret != EVDTEST_ERROR_NONE && context.error_callback) context.error_callback();

    return (void*)ret;
}

static void evdtest_event_free(evdsptc_event_t* event){
    evdtest_eventparam_t* eventparam;
    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);

    if(event->handler == evdtest_event_addobserver){
        TRACE("observer %p has been freeing...", (void*)event);
        regfree(&eventparam->regex_eventname);
    }else{
        TRACE("event %p has been freeing...", (void*)event);
    }
    free(eventparam);
    free(event);
}

static void evdtest_event_listelem_free(evdsptc_listelem_t* listelem){
    evdsptc_event_t* event = (evdsptc_event_t*)listelem;
    evdtest_event_free(event);
}

static void evdtest_event_trash_impl(evdsptc_event_t* event){
    evdsptc_listelem_t* listelem = (evdsptc_listelem_t*)event;

    event->auto_destruct = false;
    event->destructor = evdtest_event_free;
    evdsptc_listelem_setdestructor(listelem, evdtest_event_listelem_free);
    evdsptc_list_push(&context.observers_bin, &event->listelem);

    TRACE("evdtest_event %p trashed.", (void*)event);
}

static void evdtest_event_trash(evdsptc_event_t* event){
    pthread_mutex_lock(&context.mutex);
    evdtest_event_trash_impl(event);
    pthread_mutex_unlock(&context.mutex);
}

static bool evdtest_event_addobserver(evdsptc_event_t* observer){
    bool done = false;
    evdsptc_listelem_t* listelem = (evdsptc_listelem_t*)observer;
    evdtest_eventparam_t* observerparam;
    observerparam = (evdtest_eventparam_t*)evdsptc_event_getparam(observer);

    pthread_mutex_lock(&context.mutex);
    evdsptc_listelem_setdestructor(listelem, evdtest_event_listelem_free);
    if(context.state == EVDTEST_STATUS_RUNNING){
        evdsptc_list_push(&context.observers, &observer->listelem);
        TRACE("observer %p '%s' added, capture = %d", (void*)(observer), observerparam->eventname, observerparam->capture);
    }else{
        observer->auto_destruct = false;
        evdsptc_event_cancel(observer);
        TRACE("observer %p canceled", (void*)(observer));
        evdtest_event_trash_impl(observer);
    }
    pthread_mutex_unlock(&context.mutex);

    return done;
}

static bool evdtest_eventhandler(evdsptc_event_t* event){
    bool done = true;
    evdsptc_listelem_t* iterator;
    evdsptc_listelem_t copied;
    evdsptc_event_t*  observer;
    char* target_eventname;
    char* eventname;
    evdtest_eventparam_t* eventparam;
    evdtest_eventparam_t* observerparam;
    
    pthread_mutex_lock(&context.mutex);
    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);
    eventname = eventparam->eventname;
    TRACE("event %p '%s' handling...", (void*)(event), eventname);
    if(eventparam->observer_count < 0) eventparam->observer_count = EVDTEST_EVENT_TO_BE_DESTROYED;

    if(context.state != EVDTEST_STATUS_RUNNING || evdsptc_list_is_empty(&context.observers)) goto DONE;
    
    iterator = evdsptc_list_iterator(&context.observers);
    while(evdsptc_listelem_hasnext(iterator)){
        iterator = evdsptc_listelem_next(iterator);
        copied = *iterator;

        observer = (evdsptc_event_t*)iterator;
        TRACE("observer %p is checking event %p ... ", (void*)observer, (void*)event);
        observerparam = (evdtest_eventparam_t*)evdsptc_event_getparam(observer);
        target_eventname = observerparam->eventname;
        assert(&observerparam->regex_eventname);
        assert(eventname);
        if(0 == regexec(&observerparam->regex_eventname, eventname, 0, NULL, 0)){
            done = false;
            event->auto_destruct = false;
            if(eventparam->observer_count < 0) eventparam->observer_count = 0;
            eventparam->observer_count++;
            evdsptc_listelem_setdestructor((evdsptc_listelem_t*)event, evdtest_event_listelem_free);
            strncpy(observerparam->actual_eventname, eventname, (EVDTEST_EVENT_LENGTH - 1));
            observer = (evdsptc_event_t*)evdsptc_listelem_remove(iterator);
            observer = (evdsptc_event_t*)evdsptc_list_push(&context.observers_bin, iterator);
            observerparam->caught = event;
            if(eventparam->observer_count == 1) evdsptc_list_push(&context.suspended_events, (evdsptc_listelem_t*)event);
            if(observerparam->capture){
                evdtest_postevent_nop("observer %p '%s' captured event %p", (void*)observer, target_eventname, (void*)event);
            }else{
                evdtest_postevent_nop("observer %p '%s' caught event %p", (void*)observer, target_eventname, (void*)event);
            }
            evdsptc_event_done(observer);
            iterator = &copied;
            TRACE("removed !!!");
        }
    }
    if(!done) sem_post(&context.sem_lua_suspended);

DONE:
    pthread_mutex_unlock(&context.mutex);
    TRACE("event %p handled, done = %d, observer_count = %d", (void*)event, done, eventparam->observer_count);
    return done;
}

void evdtest_eventformat_simple(char* buf, evdsptc_event_t* event){
    evdtest_eventparam_t* eventparam = (evdtest_eventparam_t*)event->param;
    snprintf(buf, EVDTEST_BUFFFER_LENGTH - 1, "%s", (char*)eventparam->eventname);
}

void evdtest_eventformat_with_source(char* buf, evdsptc_event_t* event){
    evdtest_eventparam_t* eventparam = (evdtest_eventparam_t*)event->param;
    snprintf(buf, EVDTEST_BUFFFER_LENGTH - 1, "%s:%d:%s: %s", 
            (char*)eventparam->source_file, 
            eventparam->source_line, 
            eventparam->source_func,
            (char*)eventparam->eventname);
}

static void evdtest_event_queued(evdsptc_event_t* event){
    char buffer[EVDTEST_BUFFFER_LENGTH];
    evdtest_eventparam_t* eventparam = (evdtest_eventparam_t*)event->param;
    if(event->handler == evdtest_event_addobserver && evdtest_thread_is_lua(pthread_self())){
        fprintf(stdout, EVDTEST_LUA_EVENT_HEADER"observer %p waiting for event '%s' at %s:%d: in %s\n", 
                (void*)event, 
                (char*)eventparam->eventname, 
                (char*)eventparam->source_file, 
                eventparam->source_line, 
                eventparam->source_func);
    }else if(event->handler == evdtest_eventhandler_error){
        fprintf(stdout, EVDTEST_ERROR_EVENT_HEADER"%s\n", (char*)eventparam->eventname);
    }else{
        if(evdtest_thread_is_handler(pthread_self())){
            fprintf(stdout, EVDTEST_SYSTEM_EVENT_HEADER"%s\n", (char*)eventparam->eventname);
        }else if(evdtest_thread_is_lua(pthread_self())){
            fprintf(stdout, EVDTEST_LUA_EVENT_HEADER"%s\n", (char*)eventparam->eventname);
        }else{
            context.formatter(buffer, event);
            fprintf(stdout, "%s\n", buffer);
        }
    }
    fflush(stdout);
}

static evdtest_error_t evdtest_event_init(
        evdsptc_event_t** event,
        evdsptc_handler_t eventhandler,
        const char* eventname,
        bool auto_destruct,
        evdsptc_event_destructor_t event_destructor){

    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdtest_eventparam_t* eventparam;

    *event = (evdsptc_event_t*)malloc(sizeof(evdsptc_event_t));
    eventparam = (evdtest_eventparam_t*)malloc(sizeof(evdtest_eventparam_t));

    if(*event == NULL || eventparam == NULL){
        ret = EVDTEST_ERROR_OUT_OF_MEMORY;
        if(event) free(event);
        if(eventparam) free(eventparam);
        goto DONE;
    }

    strncpy(eventparam->eventname, eventname, (EVDTEST_EVENT_LENGTH - 1));
    eventparam->thread  = pthread_self();
    eventparam->capture = false;
    eventparam->caught = NULL;
    eventparam->observer_count = EVDTEST_EVENT_NOT_YET_HANDLING;

    evdsptc_event_init(*event, eventhandler, (void*)eventparam, auto_destruct, event_destructor);

DONE:
    return ret;
}

static void evdtest_strncpy(char* dest, const char* src, size_t size){
    if(src){
        strncpy(dest, src, size);
    }else dest[0] = '\0';
}

static evdtest_error_t evdtest_postevent_impl(const char* eventname, const char* file, const char* func, int line, bool lock, bool waitdone){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_event_t* pevent = NULL;
    evdtest_eventparam_t* eventparam = NULL;
    evdsptc_error_t err;
     
    if(lock) pthread_mutex_lock(&context.mutex);
    if(!(context.state == EVDTEST_STATUS_RUNNING || 
        context.state == EVDTEST_STATUS_DESTROYING)) ret = EVDTEST_ERROR_INVALID_STATE;    
    if(ret == EVDTEST_ERROR_INVALID_STATE) goto DONE;

    if(waitdone) waitdone = !evdtest_thread_is_handler(pthread_self()) && !evdtest_thread_is_lua(pthread_self());
    ret = evdtest_event_init(&pevent, evdtest_eventhandler, eventname, !waitdone, evdtest_event_free);
    if(ret != EVDTEST_ERROR_NONE) goto DONE; 

    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(pevent);
    evdtest_strncpy(eventparam->source_file, file, (EVDTEST_SOURCE_LENGTH - 1));
    evdtest_strncpy(eventparam->source_func, func, (EVDTEST_SOURCE_LENGTH - 1));
    eventparam->source_line = line;

    if(evdsptc_post(&context.evdsptc_ctx, pevent) != EVDSPTC_ERROR_NONE){
        ret = EVDTEST_ERROR_EVDSPTC_FAIL_POST;
        goto DONE;
    }

DONE:
    if(lock) pthread_mutex_unlock(&context.mutex);

    if((ret == EVDTEST_ERROR_NONE) && waitdone){
        TRACE("event %p has been waiting...", (void*)pevent);
        err = evdsptc_event_waitdone(pevent); 
        if(EVDSPTC_ERROR_CANCELED == err){
            ret = EVDTEST_ERROR_CANCELED;
        }else if(EVDSPTC_ERROR_NONE != err){
            ret = EVDTEST_ERROR_EVDSPTC_FAIL_WAIT;
        }

        if(lock) pthread_mutex_lock(&context.mutex);
        TRACE("event %p has done, observer_count = %d, err = %d, ret = %d", (void*)pevent, eventparam->observer_count, err, ret);
        if(eventparam->observer_count == 0) eventparam->observer_count = EVDTEST_EVENT_TO_BE_DESTROYED;
        else if(eventparam->observer_count == EVDTEST_EVENT_TO_BE_DESTROYED) evdsptc_event_destroy(pevent);
        if(lock) pthread_mutex_unlock(&context.mutex);
    }

    return ret;
}

evdtest_error_t evdtest_postevent(const char* format, const char* file, const char* func, int line, ...){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    va_start(list, line);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);
    return evdtest_postevent_impl(str, file, func, line, true, true);
}

evdtest_error_t evdtest_postevent_noblock(const char* format, const char* file, const char* func, int line, ...){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    va_start(list, line);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);    
    return evdtest_postevent_impl(str, file, func, line, true, false);
}

evdtest_error_t evdtest_addobserver(const char* eventname, const char* file, const char* func, int line, bool capture, int timeout, evdsptc_event_t** event){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_event_t* pevent = NULL;
    evdtest_eventparam_t* eventparam;

    pthread_mutex_lock(&context.mutex);
    if(context.state == EVDTEST_STATUS_DESTROYED) ret = EVDTEST_ERROR_INVALID_STATE;    
    if(ret == EVDTEST_ERROR_INVALID_STATE) goto DONE;
    
    ret = evdtest_event_init(&pevent, evdtest_event_addobserver, eventname, true, evdtest_event_trash);
    if(ret == EVDTEST_ERROR_NONE) {
        eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(pevent);
        if(0 != regcomp(&eventparam->regex_eventname, eventname, REG_EXTENDED | REG_NOSUB | REG_NEWLINE)) ret = EVDTEST_ERROR_FAIL_COMP_REGEX;
    }
    if(ret != EVDTEST_ERROR_NONE) goto DONE; 

    evdtest_strncpy(eventparam->source_file, file, (EVDTEST_SOURCE_LENGTH - 1));
    evdtest_strncpy(eventparam->source_func, func, (EVDTEST_SOURCE_LENGTH - 1));
    eventparam->source_line = line;

    eventparam->capture = capture;
    if(!capture){ 
        clock_gettime(CLOCK_REALTIME, &eventparam->timeout);
        if(timeout < 0) eventparam->timeout.tv_sec += context.timeout;
        else eventparam->timeout.tv_sec += timeout;
    }

    if(evdsptc_post(&context.evdsptc_ctx, pevent) != EVDSPTC_ERROR_NONE){
        evdsptc_event_destroy(pevent);
        ret = EVDTEST_ERROR_EVDSPTC_FAIL_POST;
        goto DONE;
    }

    *event = pevent;
    TRACE("addobserver event %p posted. timeout = %d, capture = %d", (void*)(*event), timeout, capture);

DONE:
    pthread_mutex_unlock(&context.mutex);
    return ret;
}

evdtest_error_t evdtest_start(evdtest_eventformat_t formatter, void (*error_callback)(void)){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_error_t err;
    char* test_file = NULL;
    int lua_ret = 0;

    if((context.done_file = fopen(EVDTEST_DONE_FILE, "w")) == NULL){
        ret = EVDTEST_ERROR_COULD_NOT_OPEN_DONEFILE;
        goto DONE;
    }

    pthread_mutex_init(&context.mutex, NULL);
    if(!formatter) context.formatter = evdtest_eventformat_simple;
    else context.formatter = formatter;
    context.timeout = 10;
    context.lua_started = false;
    context.join_kicked = false;
    context.first_lua_suspended = false;
    context.error_callback = error_callback;
    sem_init(&context.sem_lua_suspended, 0, 0);
    context.join_result = EVDTEST_ERROR_NONE;
    __sync_synchronize();

    context.L = luaL_newstate();
    luaL_openlibs(context.L);
    luaopen_evdtestc(context.L);

    test_file = getenv(EVDTEST_ENV_TEST_CASE);

    evdsptc_list_init(&context.suspended_events);
    evdsptc_list_init(&context.observers);
    evdsptc_list_init(&context.observers_bin);

    if(EVDSPTC_ERROR_NONE != (err = evdsptc_create(&context.evdsptc_ctx, evdtest_event_queued, NULL, NULL))){
        ret = EVDTEST_ERROR_EVDSPTC_FAIL_CREATE;
        context.state = EVDTEST_STATUS_ERROR_EVDSPTC_CREATE;
        goto DONE;
    };
    
    context.state = EVDTEST_STATUS_RUNNING;
    __sync_synchronize();

    if(test_file == NULL || 0 == strlen(test_file)){
        ret = EVDTEST_ERROR_NOT_FOUND;
        context.state = EVDTEST_STATUS_ERROR_LUA_LOAD_FILE;
        ret = (evdtest_error_t)evdtest_lua_handle_error(LUA_ERRFILE);
        goto DONE;
    }

    snprintf(context.start_event_name, EVDTEST_EVENT_LENGTH - 1, "test script '%s' STARTED", test_file);

    if(0 != (lua_ret = luaL_loadfile(context.L, getenv(EVDTEST_ENV_TEST_CASE)))){
        context.state = EVDTEST_STATUS_ERROR_LUA_LOAD_FILE;
        ret = (evdtest_error_t)evdtest_lua_handle_error(lua_ret);
        goto DONE;
    }

    if(0 != pthread_create(&context.lua_thread, NULL, evdtest_thread_lua, (void*) &context)){
        context.state = EVDTEST_STATUS_ERROR_LUA_THREAD_CREATE;
        ret = EVDTEST_ERROR_FAIL_CREATE_THREAD;
        goto DONE;
    }
    context.lua_started = true;

    do{
        usleep(EVDTEST_USLEEP_INTERVAL);
        __sync_synchronize();
    }while(context.first_lua_suspended == false);

DONE:
    context.join_result = ret;
    return ret;
}

evdtest_error_t evdtest_join(void){
    void* arg = NULL;
    bool wait_destroyed = false;
    bool require_join_lua = false;
    
    TRACE("evdtest join start, join_result = %d", context.join_result);

    pthread_mutex_lock(&context.mutex);
    if(!context.join_kicked) {
        context.join_kicked = true;
        if(context.lua_started) require_join_lua = true;
    }else wait_destroyed = true;
    pthread_mutex_unlock(&context.mutex);

    if(wait_destroyed){
        do{
            usleep(EVDTEST_USLEEP_INTERVAL);
            __sync_synchronize();
        }while(context.state != EVDTEST_STATUS_DESTROYED);
        goto DONE;
    }else{
        if(require_join_lua){
            TRACE("lua joining...");
            pthread_join(context.lua_thread, &arg);
            context.join_result = (evdtest_error_t)arg;
            TRACE("lua joined, join_result = %d", context.join_result);
            if(context.join_result == EVDTEST_ERROR_NONE){
                fputs(getenv(EVDTEST_ENV_TEST_CASE), context.done_file); 
                fputs("\n", context.done_file); 
                fflush(context.done_file);
            }
            fclose(context.done_file);
        }

        context.state = EVDTEST_STATUS_DESTROYING;
        __sync_synchronize();
        TRACE("destroying evdsptc...");
        evdsptc_destory(&context.evdsptc_ctx, true);

        TRACE("closing lua state...");
        lua_close(context.L);
        
        while(EVDTEST_ERROR_NOT_DONE == evdtest_wait(true)) usleep(EVDTEST_USLEEP_INTERVAL);

        TRACE("deleting suspendend events...");
        evdsptc_list_destroy(&context.suspended_events);
        TRACE("deleting observers...");
        evdsptc_list_destroy(&context.observers);
        TRACE("deleting trashed observers...");
        evdsptc_list_destroy(&context.observers_bin);
        
        __sync_synchronize();
        context.state = EVDTEST_STATUS_DESTROYED;
    }

DONE:
    TRACE("evdtest joined, join_result = %d", context.join_result);
    return context.join_result;
}

evdtest_error_t evdtest_wait(bool finalize){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_listelem_t* iterator;
    evdsptc_listelem_t copied;
    evdsptc_event_t* event;
    evdsptc_event_t* observer;
    evdtest_eventparam_t* observerparam;
    evdtest_eventparam_t* eventparam;
    struct timespec timeout = {0,0};
    struct timespec now = {0,0};
    int count = 0;

    pthread_mutex_lock(&context.mutex);
    if(context.state >= EVDTEST_STATUS_DESTROYING) finalize = true;
    TRACE("evdtest wait started, finalize = %d", finalize);
    context.first_lua_suspended = true;

    iterator = evdsptc_list_iterator(&context.suspended_events);
    while(evdsptc_listelem_hasnext(iterator)){
        count++;
        iterator = evdsptc_listelem_next(iterator);
        TRACE("suspended event %p checking...", (void*)iterator);
        copied = *iterator;
        event = (evdsptc_event_t*)iterator;
        eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);

        if(eventparam->observer_count < 0){
            TRACE("suspended event %p have been destroying...", (void*)event);
            evdsptc_listelem_remove(iterator);
            evdsptc_event_destroy(event);
            iterator = &copied;
            count--;
        }else if(eventparam->observer_count == 0){
            TRACE("suspended event %p done, is_lua = %d", (void*)event, evdtest_thread_is_lua(eventparam->thread));
            if(!evdtest_thread_is_lua(eventparam->thread)){
                evdsptc_event_done(event);
            }else{
                eventparam->observer_count = EVDTEST_EVENT_TO_BE_DESTROYED;
            }
        }else if(finalize){
            TRACE("suspended event %p have been canceling...", (void*)event);
            evdsptc_event_cancel(event);
            eventparam->observer_count = 0;
            if(evdtest_thread_is_lua(eventparam->thread)){
                eventparam->observer_count = EVDTEST_EVENT_TO_BE_DESTROYED;
            }
        }
    }

    clock_gettime(CLOCK_REALTIME, &now);
    timeout = now;
    timeout.tv_sec += context.timeout;
    iterator = evdsptc_list_iterator(&context.observers);
    while(evdsptc_listelem_hasnext(iterator)){
        iterator = evdsptc_listelem_next(iterator);
        observer = (evdsptc_event_t*)iterator;
        observerparam = (evdtest_eventparam_t*)evdsptc_event_getparam(observer);
        if( (timeout.tv_sec  >= observerparam->timeout.tv_sec  &&
             timeout.tv_nsec > observerparam->timeout.tv_nsec) ){
            timeout = observerparam->timeout;
        }
    }
    if(count > 0) ret = EVDTEST_ERROR_NOT_DONE;
    pthread_mutex_unlock(&context.mutex);

    if(!finalize){
        TRACE("evdtest waiting..., now = %d, until = %d", (int)now.tv_sec, (int)timeout.tv_sec);
        while(-1 == sem_timedwait(&context.sem_lua_suspended, &timeout) && errno == EINTR) continue;
        clock_gettime(CLOCK_REALTIME, &now);
        TRACE("evdtest resumed!!!, now = %d", (int)now.tv_sec);
    }else{

    }
    
    return ret;
}

char* evdtest_observer_geteventname(evdsptc_event_t* observer){
   return ((evdtest_eventparam_t*)evdsptc_event_getparam(observer))->actual_eventname; 
}

void evdtest_observer_destroy(evdsptc_event_t* observer){
    evdsptc_listelem_t* removed;

    pthread_mutex_lock(&context.mutex);
    removed = evdsptc_listelem_remove((evdsptc_listelem_t*)observer);
    pthread_mutex_unlock(&context.mutex);
    
    TRACE("observer %p has been destroying....", (void*)observer);
    
    if(removed->destructor != NULL) removed->destructor(removed);
}

void evdtest_abort(void){
    evdsptc_listelem_t* iterator;
    evdsptc_listelem_t copied;
    evdsptc_event_t*  observer;

    evdtest_postevent_nop("abort requested !!!");
    
    pthread_mutex_lock(&context.mutex);
    context.state = EVDTEST_STATUS_DESTROYING;
   
    iterator = evdsptc_list_iterator(&context.observers);
    while(evdsptc_listelem_hasnext(iterator)){
        iterator = evdsptc_listelem_next(iterator);
        copied = *iterator;
        observer = (evdsptc_event_t*)evdsptc_listelem_remove(iterator);
        observer->auto_destruct = false;
        evdsptc_event_cancel(observer);
        TRACE("observer %p canceled", (void*)(observer));
        evdtest_event_trash_impl(observer);
        iterator = &copied;
    }
    pthread_mutex_unlock(&context.mutex);
    TRACE("abort sem posted...");
    sem_post(&context.sem_lua_suspended);
}

evdtest_error_t evdtest_observer_trywait(evdsptc_event_t* observer){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_error_t err;

    evdsptc_listelem_t* iterator;
    evdsptc_listelem_t copied;

    evdtest_eventparam_t* eventparam;
    evdtest_eventparam_t* observerparam;
    struct timespec now;
    bool resume = false;

    err = evdsptc_event_trywaitdone(observer);
    TRACE("observer %p trywaitdone, err = %d", (void*)observer, err);
    observerparam = (evdtest_eventparam_t*)evdsptc_event_getparam(observer);
    if(EVDSPTC_ERROR_NOT_DONE == err){
        if(observerparam->capture) ret = EVDTEST_ERROR_NOT_DONE;
        else{
            pthread_mutex_lock(&context.mutex);
            clock_gettime(CLOCK_REALTIME, &now);
            if( now.tv_sec  >= observerparam->timeout.tv_sec  &&
                    now.tv_nsec >= observerparam->timeout.tv_nsec ){
                resume = false;
                iterator = evdsptc_list_iterator(&context.observers);
                while(evdsptc_listelem_hasnext(iterator)){
                    iterator = evdsptc_listelem_next(iterator);
                    copied = *iterator;

                    if(observer == (evdsptc_event_t*)iterator){
                        evdsptc_listelem_remove(iterator);
                        observer->auto_destruct = false;
                        TRACE("observer %p canceled due to timeout", (void*)(observer));
                        evdsptc_event_cancel(observer);
                        evdsptc_list_push(&context.observers_bin, iterator);
                        iterator = &copied;
                        resume = true;
                    }
                }
                if(resume) sem_post(&context.sem_lua_suspended);
                ret = EVDTEST_ERROR_TIMEOUT;
            }else ret = EVDTEST_ERROR_NOT_DONE;
            pthread_mutex_unlock(&context.mutex);
        }
    }else if(EVDSPTC_ERROR_CANCELED == err){
        ret = EVDTEST_ERROR_CANCELED;
    }else if(EVDSPTC_ERROR_NONE != err){
        ret = EVDTEST_ERROR_EVDSPTC_FAIL_WAIT;
    }

    if(
            ret == EVDTEST_ERROR_NONE    ||
            ret == EVDTEST_ERROR_TIMEOUT ||
            ret == EVDTEST_ERROR_CANCELED
      ){
        pthread_mutex_lock(&context.mutex);
        if(observerparam->capture == false && observerparam->caught){
            eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(observerparam->caught);
            eventparam->observer_count--;
            TRACE("observer %p trywaitdone event %p observer_count = %d, err = %d, ret = %d", (void*)observer, (void*)observerparam->caught, eventparam->observer_count, err, ret);
        }
        pthread_mutex_unlock(&context.mutex);
    }

    return ret;
}

void evdtest_setdefaulttimeout(int timeout){
    context.timeout = timeout;
}

void evdtest_observer_releaseevent(evdsptc_event_t* observer){
    evdtest_eventparam_t* observerparam;
    evdtest_eventparam_t* eventparam;
    evdsptc_event_t* event;

    pthread_mutex_lock(&context.mutex);
    observerparam = (evdtest_eventparam_t*)evdsptc_event_getparam(observer);
    assert(observer);
    assert(observerparam);
    event = observerparam->caught;
    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);
    eventparam->observer_count--;
    TRACE("observer %p releasing event %p, observer_count = %d", (void*)observer, (void*)event, eventparam->observer_count);

    if(eventparam->observer_count == 0){
        observerparam->caught->auto_destruct = false;
        evdsptc_event_cancel(observerparam->caught);
        TRACE("observer %p released event %p.", (void*)observer, (void*)observerparam->caught);
    }
    pthread_mutex_unlock(&context.mutex);
}
