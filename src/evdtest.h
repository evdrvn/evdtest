#ifndef __EVDTEST_H__
#define __EVDTEST_H__

#ifdef __cplusplus
extern "C" {

#endif

#include <lauxlib.h>
#include <lualib.h>
#include <lauxlib.h>
#include <evdsptc.h>
#include <sys/types.h>
#include <regex.h>
#include <time.h>

#ifdef EVDTRACE
#define TRACE(fmt, ...) printf("##TRACE## " fmt "\n",##__VA_ARGS__); fflush(stdout)
#else
#define TRACE(fmt, ...) (void)sizeof(printf(fmt,##__VA_ARGS__))
#endif

#define EVDTEST_TEST_CASE "EVDTEST_TEST_CASE"
#define EVDTEST_TEST_DONE "EVDTEST_TEST_DONE"

#define EVDTEST_SYSTEM_EVENT_HEADER "[EVDTEST] "
#define EVDTEST_LUA_EVENT_HEADER "[EVDTLUA] "
#define EVDTEST_ERROR_EVENT_HEADER "[EVDTERR] "

#define EVDTEST_EVENT_LENGTH (512)
#define EVDTEST_FUNC_LENGTH (128)

typedef enum{
    EVDTEST_ERROR_NONE = 0,
    EVDTEST_ERROR_EVDSPTC_FAIL_CREATE,
    EVDTEST_ERROR_FAIL_CREATE_THREAD,
    EVDTEST_ERROR_BAD_LUA_SCRIPT,
    EVDTEST_ERROR_ANY_LUA_EXCEPTIONS,
    EVDTEST_ERROR_INVALID_STATE,
    EVDTEST_ERROR_EVDSPTC_FAIL_POST,
    EVDTEST_ERROR_OUT_OF_MEMORY,
    EVDTEST_ERROR_NOT_FOUND,
    EVDTEST_ERROR_FAIL_COMP_REGEX,
    EVDTEST_ERROR_TIMEOUT,
    EVDTEST_ERROR_CANCELED,
    EVDTEST_ERROR_NOT_DONE,
    EVDTEST_ERROR_EVDSPTC_FAIL_WAIT
} evdtest_error_t;

typedef enum{
    EVDTEST_STATUS_ERROR_EVDSPTC_CREATE = -3,
    EVDTEST_STATUS_ERROR_LUA_LOAD_FILE = -2,
    EVDTEST_STATUS_ERROR_LUA_THREAD_CREATE = -1,
    EVDTEST_STATUS_RUNNING = 0,
    EVDTEST_STATUS_DESTROYING,
    EVDTEST_STATUS_DESTROYED
} evdtest_status_t;

typedef struct {
    evdsptc_context_t evdsptc_ctx;
    lua_State* L;
    pthread_t lua_thread;
    evdtest_status_t state;
    bool lua_started;
    pthread_mutex_t mutex;
    evdsptc_list_t suspended_events;
    evdsptc_list_t observers;
    evdsptc_list_t observers_bin;
    char start_event_name[EVDTEST_EVENT_LENGTH];
    bool join_kicked;
    evdtest_error_t join_result;
    bool first_lua_suspended;
    sem_t sem_lua_suspended;
    int timeout;
} evdtest_context_t;

typedef struct {
    char eventname[EVDTEST_EVENT_LENGTH];
    regex_t regex_eventname;
    char actual_eventname[EVDTEST_EVENT_LENGTH];
    char submitter[EVDTEST_FUNC_LENGTH];
    pthread_t thread;
    struct timespec timeout;
    int observer_count;
    bool capture;
    evdsptc_event_t* caught;
} evdtest_eventparam_t;

extern evdtest_error_t evdtest_start(void);
extern evdtest_error_t evdtest_join(void);
extern evdtest_error_t evdtest_addobserver(const char* pattern, const char* submitter, bool capture, int timeout, evdsptc_event_t** event);
extern evdtest_error_t evdtest_postevent(const char* format, ...);
extern evdtest_error_t evdtest_wait(bool finalize);
extern char* evdtest_observer_geteventname(evdsptc_event_t* event);
extern void evdtest_observer_destroy(evdsptc_event_t* event);
extern void evdtest_abort(void);
extern evdtest_error_t evdtest_observer_trywait(evdsptc_event_t* event);
extern void evdtest_setdefaulttimeout(int timeout);
extern void  evdtest_observer_releaseevent(evdsptc_event_t* event);

#ifdef __cplusplus
}
#endif

#endif
