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
#endif

    return 0;
}
