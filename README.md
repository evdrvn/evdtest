# evdtest
evdtest is an event-driven test framework. Its purpose is to automate system testing of multi-threaded applications. Its main features and design principles are:

* Test scenario as lua code
* Use debug log for synchronization between test scenario and application threads
* Easy embedding to target application

## Minimal example

Test target (examples/minimal/minimal.c) of this example is as follows (TEST_MINIMAL switch is all the code required for embedding):
```c
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "pthread.h"
#include "evdtest.h"

#ifdef TEST_MINIMAL
#define LOG_DEBUG(fmt, ...) EVDTEST_POSTEVENT(fmt, ##__VA_ARGS__) 
#else
#define LOG_DEBUG(fmt, ...) printf("##DEBUG## %p: " fmt "\n", (void*)pthread_self(), ##__VA_ARGS__); fflush(stdout) 
#endif

static int sum = 0;
static pthread_t th;

static void cancel(void){
    pthread_cancel(th);
}

static void* recv_and_add(void* arg){
    int sock = (int)arg;
    int num = 0;
    char buf[2];
    while(recv(sock, &buf, sizeof(buf), 0)){
        buf[1] = '\0';
        num = atoi(buf);
        LOG_DEBUG("received:%d", num);
        sum += num;
        if(sum >= 6) break;
    }
    LOG_DEBUG("thread done");
    return NULL;
}

int main(int ac, char** av){
    (void) ac;
    (void) av;
    int sock;
    struct sockaddr_in addr;

#ifdef TEST_MINIMAL
    evdtest_start(NULL, cancel);
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    LOG_DEBUG("binded");

    pthread_create(&th, NULL, recv_and_add, (void*)sock);
    pthread_join(th, NULL);

#ifdef TEST_MINIMAL
    evdtest_join();
    evdtest_destroy();
#endif

    return 0;
}
```

Test scenario (examples/minimal/test_minimal.lua) of this example is as follows:
```lua
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

```

Test result of this expamle is as follows:

```txt
$ sh ./test_minimal.sh
[EVDTLUA] test script 'test_minimal.lua' STARTED
[EVDTLUA] observer 0xb650b110 waiting for event 'binded' at test_minimal.lua:5: in main chunk
binded
[EVDTEST] observer 0xb650b110 'binded' caught event 0x856d918
[EVDTLUA] observer 0xb650dd08 waiting for event 'received:3' at test_minimal.lua:21: in function <test_minimal.lua:20>
[EVDTLUA] observer 0xb650ff08 waiting for event 'received:2' at test_minimal.lua:16: in function <test_minimal.lua:15>
[EVDTLUA] observer 0xb65120d8 waiting for event 'received:1' at test_minimal.lua:11: in function <test_minimal.lua:10>
[EVDTLUA] observer 0xb6513e80 waiting for event 'thread done' at test_minimal.lua:30: in main chunk
received:1
[EVDTEST] observer 0xb65120d8 'received:1' caught event 0xb6400470
received:2
[EVDTEST] observer 0xb650ff08 'received:2' caught event 0xb64009f0
received:3
[EVDTEST] observer 0xb650dd08 'received:3' caught event 0xb6400470
thread done
[EVDTEST] observer 0xb6513e80 'thread done' caught event 0xb64009f0
```

If the test scenario is done without error, the scenario file name is recorded in the file 'evdtest_done.txt':
```txt
test_minimal.lua
```

## How to install

For linux:
* Get sources

    ```shell
    $ git clone https://github.com/evdrvn/evdtest
    $ cd evdtest
    $ git submodule init
    $ git suhmodule update
    ```

* Build [evdsptc](https://github.com/evdrvn/evdsptc)

    ```shell
    $ cd ext/evdsptc/build
    $ cmake ..
    $ make
	$ cd ../../..
    ```

* Build [Lua](https://www.lua.org/)

    ```shell
    $ cd ext
    $ tar xzvf lua-5.2.4.tar.gz
    $ cd lua-5.2.4
    $ make linux 
	$ cd ../..
    ```

* Build [SWIG](http://www.swig.org/)

    ```shell
    $ cd ext
    $ sh ./buid-swig.sh
	$ cd ..
    ```

* Build [evdtest](https://github.com/evdrvn/evdtest)

    ```shell
    $ make 
    ```
 
* Embed below libraries into the test target application:
    * ext/lua-5.2.4/src/liblua.a
    * ext/evdsptc/build/libevdsptc.a
    * src/libevdtest.a

## How to run tests of 'evdtest'

For linux:
* Build [Cpputest](https://cpputest.github.io/)

    ```shell
	$ cd ext/evdsptc
	$ git submodule init
	$ git submodule update
	$ cd test
    $ sh ./build_cpputest.sh
	$ cd ../../..
    ```

* Build [evdsptc](https://github.com/evdrvn/evdsptc) for test

    ```shell
	$ cd ext/evdsptc/test
	$ make
	$ cd ../../..
    ```

* Run tests

    ```shell
	$ cd test
	$ make
    ```

