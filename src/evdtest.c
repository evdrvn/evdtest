#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "evdtest.h"

extern int luaopen_evdtestc(lua_State* L);

static evdtest_error_t evdtest_event_init(
        evdsptc_event_t** event,
        evdsptc_handler_t event_handler,
        const char* eventname,
        bool auto_destruct,
        evdsptc_event_destructor_t event_destructor);

static void evdtest_event_free(evdsptc_event_t* event);
static bool evdtest_event_addobserver(evdsptc_event_t* event);

evdtest_context_t context;

evdtest_context_t* evdtest_getcontext(void){
    return &context;    
}

static bool evdtest_is_system_thread(pthread_t thread){
    return pthread_equal(thread, evdsptc_getthread(&context.evdsptc_ctx));
}

static bool evdtest_is_lua_thread(pthread_t thread){
    return  pthread_equal(thread, context.lua_thread);
}

static bool evdtest_error_event_handler(evdsptc_event_t* event){
   (void)event;
   return true;
}

static evdtest_error_t evdtest_post_error_event(char* format, ... ){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    evdsptc_event_t* pevent = NULL;
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    va_start(list, format);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);
 
    ret = evdtest_event_init(&pevent, evdtest_error_event_handler, str, true, evdtest_event_free);
    if(EVDTEST_ERROR_NONE != ret) goto DONE; 
    if(evdsptc_post(&context.evdsptc_ctx, pevent)) ret = EVDTEST_ERROR_EVDSPTC_FAIL_POST;

DONE:
    return ret;
}

static evdtest_error_t evdtest_post_nop_event(char* format, ... ){
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

static evdtest_error_t evdtest_handle_lua_error(int lua_ret){
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
        evdtest_post_error_event("lua test script '%s' not found !!", getenv(EVDTEST_TEST_CASE));
    } else if(lua_ret == LUA_ERRSYNTAX){
        ret = EVDTEST_ERROR_BAD_LUA_SCRIPT;
        evdtest_post_error_event("lua test script error !!\n%s", error);
    } else {
        ret = EVDTEST_ERROR_ANY_LUA_EXCEPTIONS;
        evdtest_post_error_event("lua error(%d) detected !!\n%s", lua_ret, error);
    }

DONE:
    return ret;
}

static void* evdtest_lua_thread_routine(void* arg){
    (void)arg;
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    int lua_ret = 0;

    evdtest_post_nop_event(context.start_event_name);
    if(0 != (lua_ret = lua_pcall(context.L, 0, 0, 0))){
        evdtest_wait(true);
        ret = evdtest_handle_lua_error(lua_ret);
    }else{
        lua_getglobal(context.L, "__evdtest_checkdone");
        if(0 != (lua_ret = lua_pcall(context.L, 0, 0, 0))){
            ret = evdtest_handle_lua_error(lua_ret);
        }
        evdtest_wait(true);
    }
    context.first_lua_suspended = true;
    TRACE("lua thread done, ret = %d", ret);
    __sync_synchronize();

    return (void*)ret;
}

static void evdtest_event_free(evdsptc_event_t* event){
    evdtest_eventparam_t* eventparam;
    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);

    if(event->handler == evdtest_event_addobserver){
        TRACE("thread %p freeing observer %p", (void*)pthread_self(), (void*)event);
        regfree(&eventparam->regex_eventname);
    }else{
        TRACE("thread %p freeing event %p", (void*)pthread_self(), (void*)event);
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

static bool evdtest_event_addobserver(evdsptc_event_t* event){
    bool done = false;
    evdsptc_listelem_t* listelem = (evdsptc_listelem_t*)event;
    evdtest_eventparam_t* eventparam;
    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);

    pthread_mutex_lock(&context.mutex);
    evdsptc_listelem_setdestructor(listelem, evdtest_event_listelem_free);
    if(context.state == EVDTEST_STATUS_RUNNING){
        evdsptc_list_push(&context.observers, &event->listelem);
        TRACE("observer %p '%s' added, capture = %d", (void*)(event), eventparam->eventname, eventparam->capture);
    }else{
        event->auto_destruct = false;
        evdsptc_event_cancel(event);
        TRACE("observer %p canceled", (void*)(event));
        evdtest_event_trash_impl(event);
    }
    pthread_mutex_unlock(&context.mutex);

    return done;
}

static bool evdtest_event_handler(evdsptc_event_t* event){
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
                evdtest_post_nop_event("observer %p '%s' captured event %p", (void*)observer, target_eventname, (void*)event);
            }else{
                evdtest_post_nop_event("observer %p '%s' caught event %p", (void*)observer, target_eventname, (void*)event);
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

static void evdtest_event_queued(evdsptc_event_t* event){
    evdtest_eventparam_t* eventparam = (evdtest_eventparam_t*)event->param;
    if(event->handler == evdtest_event_addobserver && evdtest_is_lua_thread(pthread_self())){
        fprintf(stdout, EVDTEST_LUA_EVENT_HEADER"observer %p waiting for event '%s' at %s\n", (void*)event, (char*)eventparam->eventname, (char*)eventparam->submitter);
    }else if(event->handler == evdtest_event_addobserver){
        fprintf(stdout, "observer %p waiting for event '%s'\n", (void*)event, (char*)eventparam->eventname);
    }else if(event->handler == evdtest_error_event_handler){
        fprintf(stdout, EVDTEST_ERROR_EVENT_HEADER"%s\n", (char*)eventparam->eventname);
    }else{
        if(evdtest_is_system_thread(pthread_self())){
            fprintf(stdout, EVDTEST_SYSTEM_EVENT_HEADER"%s\n", (char*)eventparam->eventname);
        }else if(evdtest_is_lua_thread(pthread_self())){
            fprintf(stdout, EVDTEST_LUA_EVENT_HEADER"%s\n", (char*)eventparam->eventname);
        }else{
            fprintf(stdout, "%s\n", (char*)eventparam->eventname);
        }
    }
    fflush(stdout);
}

static evdtest_error_t evdtest_event_init(
        evdsptc_event_t** event,
        evdsptc_handler_t event_handler,
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
    eventparam->observer_count = -1;

    evdsptc_event_init(*event, event_handler, (void*)eventparam, auto_destruct, event_destructor);

DONE:
    return ret;
}

static evdtest_error_t evdtest_postevent_impl(const char* eventname, bool lock, bool waitdone){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_event_t* pevent = NULL;
    evdtest_eventparam_t* eventparam = NULL;
    evdsptc_error_t err;
     
    if(lock) pthread_mutex_lock(&context.mutex);
    if(!(context.state == EVDTEST_STATUS_RUNNING || 
        context.state == EVDTEST_STATUS_DESTROYING)) ret = EVDTEST_ERROR_INVALID_STATE;    
    if(ret == EVDTEST_ERROR_INVALID_STATE) goto DONE;

    if(waitdone) waitdone = !evdtest_is_system_thread(pthread_self()) && !evdtest_is_lua_thread(pthread_self());
    ret = evdtest_event_init(&pevent, evdtest_event_handler, eventname, !waitdone, evdtest_event_free);
    if(ret != EVDSPTC_ERROR_NONE) goto DONE; 

    if(evdsptc_post(&context.evdsptc_ctx, pevent) != EVDSPTC_ERROR_NONE){
        ret = EVDTEST_ERROR_EVDSPTC_FAIL_POST;
        goto DONE;
    }

DONE:
    if(lock) pthread_mutex_unlock(&context.mutex);

    if((ret == EVDTEST_ERROR_NONE) && waitdone){
        err =evdsptc_event_waitdone(pevent); 
        if(EVDSPTC_ERROR_CANCELED == err){
            ret = EVDTEST_ERROR_CANCELED;
        }else if(EVDSPTC_ERROR_NONE != err){
            ret = EVDTEST_ERROR_EVDSPTC_FAIL_WAIT;
        }

        if(lock) pthread_mutex_lock(&context.mutex);
        eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(pevent);
        TRACE("event %p has done, observer_count = %d, err = %d, ret = %d", (void*)pevent, eventparam->observer_count, err, ret);
        if(eventparam->observer_count == 0) eventparam->observer_count = -1;
        else if(eventparam->observer_count < 0)evdsptc_event_destroy(pevent);
        if(lock) pthread_mutex_unlock(&context.mutex);
    }

    return ret;
}

evdtest_error_t evdtest_postevent(const char* format, ...){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    va_start(list, format);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);
    return evdtest_postevent_impl(str, true, true);
}

evdtest_error_t evdtest_postevent_noblock(const char* format, ...){
    va_list list;
    char str[EVDTEST_EVENT_LENGTH];
    va_start(list, format);
    vsnprintf(str, EVDTEST_EVENT_LENGTH - 1, format, list);
    va_end(list);    
    return evdtest_postevent_impl(str, true, false);
}

evdtest_error_t evdtest_addobserver(const char* eventname, const char* submitter, bool capture, int timeout, evdsptc_event_t** event){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_event_t* pevent = NULL;
    evdtest_eventparam_t* eventparam;

    pthread_mutex_lock(&context.mutex);
    if(context.state == EVDTEST_STATUS_DESTROYED) ret = EVDTEST_ERROR_INVALID_STATE;    
    if(ret == EVDTEST_ERROR_INVALID_STATE) goto DONE;
    
    ret = evdtest_event_init(&pevent, evdtest_event_addobserver, eventname, true, evdtest_event_trash);
    if(ret == EVDSPTC_ERROR_NONE) {
        eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(pevent);
        if(0 != regcomp(&eventparam->regex_eventname, eventname, REG_EXTENDED | REG_NOSUB | REG_NEWLINE)) ret = EVDTEST_ERROR_FAIL_COMP_REGEX;
    }
    if(ret != EVDSPTC_ERROR_NONE) goto DONE; 
    
    if(submitter) strncpy(eventparam->submitter, submitter, (EVDTEST_FUNC_LENGTH - 1));
    else eventparam->submitter[0] = '\0';

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

evdtest_error_t evdtest_start(void){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_error_t err;
    char* test_file = NULL;
    int lua_ret = 0;

    setenv(EVDTEST_TEST_DONE, "FALSE", 1);

    pthread_mutex_init(&context.mutex, NULL);
    context.timeout = 10;
    context.lua_started = false;
    context.join_kicked = false;
    context.first_lua_suspended = false;
    sem_init(&context.sem_lua_suspended, 0, 0);
    context.join_result = EVDTEST_ERROR_NONE;
    __sync_synchronize();

    context.L = luaL_newstate();
    luaL_openlibs(context.L);
    luaopen_evdtestc(context.L);

    test_file = getenv(EVDTEST_TEST_CASE);

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
        ret = (evdtest_error_t)evdtest_handle_lua_error(LUA_ERRFILE);
        goto DONE;
    }

    snprintf(context.start_event_name, EVDTEST_EVENT_LENGTH - 1, "test script '%s' STARTED", test_file);

    if(0 != (lua_ret = luaL_loadfile(context.L, getenv(EVDTEST_TEST_CASE)))){
        context.state = EVDTEST_STATUS_ERROR_LUA_LOAD_FILE;
        ret = (evdtest_error_t)evdtest_handle_lua_error(lua_ret);
        goto DONE;
    }

    if(0 != pthread_create(&context.lua_thread, NULL, evdtest_lua_thread_routine, (void*) &context)){
        context.state = EVDTEST_STATUS_ERROR_LUA_THREAD_CREATE;
        ret = EVDTEST_ERROR_FAIL_CREATE_THREAD;
        goto DONE;
    }
    context.lua_started = true;

    do{
        usleep(10);
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
            usleep(100 * 1000);
            __sync_synchronize();
        }while(context.state != EVDTEST_STATUS_DESTROYED);
        goto DONE;
    }else{
        if(require_join_lua){
            TRACE("lua joining...");
            pthread_join(context.lua_thread, &arg);
            context.join_result = (evdtest_error_t)arg;
            TRACE("lua joined, join_result = %d", context.join_result);
            if(context.join_result == EVDTEST_ERROR_NONE) setenv(EVDTEST_TEST_DONE, "TRUE", 1);
        }

        context.state = EVDTEST_STATUS_DESTROYING;
        __sync_synchronize();
        TRACE("destroying evdsptc...");
        evdsptc_destory(&context.evdsptc_ctx, true);

        TRACE("closing lua state...");
        lua_close(context.L);
        
        while(EVDTEST_ERROR_NOT_DONE == evdtest_wait(true)) usleep(10);

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

        if(
                eventparam->observer_count < 0 ||
                (finalize && evdtest_is_lua_thread(eventparam->thread))

          ){
            TRACE("suspended event %p have already been done and destroyed", (void*)event);
            evdsptc_listelem_remove(iterator);
            evdsptc_event_destroy(event);
            iterator = &copied;
            count--;
        }else if(eventparam->observer_count == 0){
            TRACE("suspended event %p done", (void*)event);
            evdsptc_event_done(event);
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
    }
    
    return ret;
}

char* evdtest_observer_geteventname(evdsptc_event_t* event){
   return ((evdtest_eventparam_t*)evdsptc_event_getparam(event))->actual_eventname; 
}

void evdtest_observer_destroy(evdsptc_event_t* event){
    evdsptc_listelem_t* removed;

    pthread_mutex_lock(&context.mutex);
    removed = evdsptc_listelem_remove((evdsptc_listelem_t*)event);
    pthread_mutex_unlock(&context.mutex);
    
    TRACE("observer %p has been destroying....", (void*)event);
    
    if(removed->destructor != NULL) removed->destructor(removed);
}

void evdtest_abort(void){
    evdsptc_listelem_t* iterator;
    evdsptc_listelem_t copied;
    evdsptc_event_t*  observer;

    evdtest_post_nop_event("abort requested !!!");
    
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

evdtest_error_t evdtest_observer_trywait(evdsptc_event_t* event){
    evdtest_error_t ret = EVDTEST_ERROR_NONE;
    evdsptc_error_t err;

    evdsptc_listelem_t* iterator;
    evdsptc_listelem_t copied;

    evdtest_eventparam_t* eventparam;
    evdtest_eventparam_t* observerparam;
    struct timespec now;
    bool resume = false;

    err = evdsptc_event_trywaitdone(event);
    TRACE("observer %p trywaitdone, err = %d", (void*)event, err);
    observerparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);
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

                    if(event == (evdsptc_event_t*)iterator){
                        evdsptc_listelem_remove(iterator);
                        event->auto_destruct = false;
                        TRACE("observer %p canceled due to timeout", (void*)(event));
                        evdsptc_event_cancel(event);
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
            TRACE("observer %p trywaitdone event %p observer_count = %d, err = %d, ret = %d", (void*)event, (void*)observerparam->caught, eventparam->observer_count, err, ret);
        }
        pthread_mutex_unlock(&context.mutex);
    }

    return ret;
}

void evdtest_setdefaulttimeout(int timeout){
    context.timeout = timeout;
}

void evdtest_observer_releaseevent(evdsptc_event_t* event){
    evdtest_eventparam_t* eventparam;
    evdtest_eventparam_t* caughtparam;
    evdsptc_event_t* caught;

    pthread_mutex_lock(&context.mutex);
    eventparam = (evdtest_eventparam_t*)evdsptc_event_getparam(event);
    assert(event);
    assert(eventparam);
    caught = eventparam->caught;
    caughtparam = (evdtest_eventparam_t*)evdsptc_event_getparam(caught);
    caughtparam->observer_count--;
    TRACE("observer %p releasing event %p, observer_count = %d", (void*)event, (void*)caught, caughtparam->observer_count);

    if(caughtparam->observer_count == 0){
        eventparam->caught->auto_destruct = false;
        evdsptc_event_cancel(eventparam->caught);
        TRACE("observer %p released event %p.", (void*)event, (void*)eventparam->caught);
    }
    pthread_mutex_unlock(&context.mutex);
}
