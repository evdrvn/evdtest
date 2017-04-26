#include <evdsptc.h>
#include "evdtest.h"

#include <stdio.h>

#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/TestHarness.h>
#include <CppUTestExt/MockSupport.h>

#define USLEEP_PERIOD (10000)
#define NUM_OF_USLEEP (10)

extern "C" {
extern evdtest_context_t* evdtest_getcontext(void);
}
static volatile int count = 0;
static volatile int relay_count = 0;

static bool countup(evdsptc_event_t* event){
    (void)event;
    count++;
    evdtest_postevent("count=%d", count);
    return true;
}

static bool relay(evdsptc_event_t* event){
    evdsptc_context_t* ctx;
    evdsptc_event_t* ev;
    relay_count++;
    ctx = (evdsptc_context_t*)evdsptc_event_getparam(event);
    ev = (evdsptc_event_t*)malloc(sizeof(evdsptc_event_t));
    evdsptc_event_init(ev, countup, NULL, true, evdsptc_event_free);
    evdsptc_post(ctx, ev);
    evdtest_postevent("relay-%d", relay_count);
    return true;
}

TEST_GROUP(evdtest_test_group){
    void setup(){
        count = 0;
        relay_count = 0;
    }
    void teardown(){
        mock().checkExpectations();
        mock().clear();
        setenv(EVDTEST_TEST_CASE, "", 1);
    }
};

TEST(evdtest_test_group, start_error_envarg_empty_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;
    
    ret_start = evdtest_start();
    ret_stop  = evdtest_join();

    CHECK_EQUAL(EVDTEST_ERROR_NOT_FOUND, ret_start);
    CHECK_EQUAL(EVDTEST_ERROR_NOT_FOUND, ret_stop);
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, start_error_bad_lua_script_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_error.lua", 1);

    ret_start = evdtest_start();
    ret_stop  = evdtest_join();

    CHECK_EQUAL(EVDTEST_ERROR_BAD_LUA_SCRIPT, ret_start);
    CHECK_EQUAL(EVDTEST_ERROR_BAD_LUA_SCRIPT, ret_stop);
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, start_error_runtime_lua_script_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_runtime_error.lua", 1);

    ret_start = evdtest_start();
    ret_stop  = evdtest_join();

    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    CHECK_EQUAL(EVDTEST_ERROR_ANY_LUA_EXCEPTIONS, ret_stop);
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, start_and_join_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_normal.lua", 1);

    ret_start = evdtest_start();
    evdtest_postevent("hello world");
    ret_stop  = evdtest_join();

    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_stop);
    STRCMP_EQUAL("TRUE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, addobserver_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;
    evdsptc_event_t* event[5];
    
    setenv(EVDTEST_TEST_CASE, "lua/test_normal.lua", 1);

    ret_start = evdtest_start();
    evdtest_addobserver("1", NULL, false, 0, &event[0]);
    evdtest_addobserver("2", NULL, false, 0, &event[1]);
    evdtest_addobserver("3", NULL, false, 0, &event[2]);
    evdtest_addobserver("4", NULL, false, 0, &event[3]);
    evdtest_addobserver("5", NULL, false, 0, &event[4]);
    evdtest_postevent("hello world");
    ret_stop  = evdtest_join();
    
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_stop);
    STRCMP_EQUAL("TRUE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, removeobserver_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;
    evdsptc_event_t *event[5];
    int i = 0;

    setenv(EVDTEST_TEST_CASE, "lua/test_normal.lua", 1);

    ret_start = evdtest_start();
    evdtest_addobserver("1", NULL, false, 0, &event[0]);
    evdtest_addobserver("2", NULL, false, 0, &event[1]);
    evdtest_addobserver("3", NULL, false, 0, &event[2]);
    evdtest_addobserver("4", NULL, false, 0, &event[3]);
    evdtest_addobserver("5", NULL, false, 0, &event[4]);
    TRACE("addobserve event[0] %p", (void*)event[0]);
    TRACE("addobserve event[1] %p", (void*)event[1]);
    TRACE("addobserve event[2] %p", (void*)event[2]);
    TRACE("addobserve event[3] %p", (void*)event[3]);
    TRACE("addobserve event[4] %p", (void*)event[4]);
    evdtest_postevent("hello world");
    evdtest_postevent("1");
    evdtest_postevent("2");
    evdtest_postevent("3");
    evdtest_postevent("4");
    evdtest_postevent("5");

    while(i++ < NUM_OF_USLEEP){
        __sync_synchronize();
        if(evdsptc_list_is_empty(&(evdtest_getcontext()->observers))) break;
        usleep(USLEEP_PERIOD);    
    }
    CHECK(evdsptc_list_is_empty(&(evdtest_getcontext()->observers)));
   
    ret_stop  = evdtest_join();

    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_stop);
    STRCMP_EQUAL("TRUE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, lua_postevent_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_postevent.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    evdtest_postevent("hello world");

    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_stop);
    
    STRCMP_EQUAL("TRUE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, abort_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_postevent.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    evdtest_abort();

    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_ANY_LUA_EXCEPTIONS, ret_stop);
    
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, timeout_main_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_timeout_main.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_ANY_LUA_EXCEPTIONS, ret_stop);
    
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, timeout_coroutine_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_timeout_coroutine.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_ANY_LUA_EXCEPTIONS, ret_stop);
    
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, ignore_cancel_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_ignore_cancel.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    evdtest_postevent("hello world");
    
    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_stop);
    
    STRCMP_EQUAL("TRUE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, residue_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;

    setenv(EVDTEST_TEST_CASE, "lua/test_residue.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    evdtest_postevent("hello world");
    
    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_ANY_LUA_EXCEPTIONS, ret_stop);
    
    STRCMP_EQUAL("FALSE", getenv(EVDTEST_TEST_DONE));
}

TEST(evdtest_test_group, capture_test){
    evdtest_error_t ret_start = EVDTEST_ERROR_NONE;
    evdtest_error_t ret_stop  = EVDTEST_ERROR_NONE;
    evdsptc_context_t ctx_relay;
    evdsptc_context_t ctx_countup;
    evdsptc_event_t* ev;
    int i = 0;

    evdsptc_create(&ctx_relay, NULL, NULL, NULL);
    evdsptc_create(&ctx_countup, NULL, NULL, NULL);

    setenv(EVDTEST_TEST_CASE, "lua/test_capture.lua", 1);

    ret_start = evdtest_start();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_start);
    
    evdtest_postevent("hello world");

    for(i = 0; i <= 9; i++){
        ev = (evdsptc_event_t*)malloc(sizeof(evdsptc_event_t));
        evdsptc_event_init(ev, relay, (void*)&ctx_countup, true, evdsptc_event_free);
        evdsptc_post(&ctx_relay, ev);
    }
    
    ret_stop  = evdtest_join();
    CHECK_EQUAL(EVDTEST_ERROR_NONE, ret_stop);
    
    evdsptc_destory(&ctx_relay, true); 
    evdsptc_destory(&ctx_countup, true); 
    
    STRCMP_EQUAL("TRUE", getenv(EVDTEST_TEST_DONE));
}

int main(int ac, char** av){
    return CommandLineTestRunner::RunAllTests(ac, av);
}


