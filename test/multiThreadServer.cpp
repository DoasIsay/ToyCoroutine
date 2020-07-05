#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include "scheduler.h"

bool isExit = false;
extern int setNoBlock(int fd, int block=1);
int socketHandleCoroutine(void *arg){
    char buf[256];
    int fd = *(int*)arg;
    
    log(INFO, "start socketHandleCoroutine fd:%d", fd);
    
    while(!isExit){
        int ret = read(fd, buf, 19);
        if(ret <= 0)
            break;
        log(INFO, "fd:%d recv %s", fd, buf);
        
        ret = write(fd, buf, 19);
        if(ret <= 0){
            log(ERROR, "fd:%d write error:%s\n", fd, strerror(errno));
            break;
        }
    }
    close(fd);
    delete (int*)arg;
    
    return 0;
}

int acceptCoroutine(void *arg){
    int serverFd = dup(*(int*)arg);
    
    while(!isExit){
        int *clientFd = new int;
        
        if((*clientFd = accept(serverFd, NULL ,NULL)) != -1){
            log(INFO, "accept fd %d", *clientFd);
            createCoroutine(socketHandleCoroutine, (void*)clientFd);
        }else{
            log(ERROR, "accept error:%s", strerror(errno));
            break;
        }
    }
    
    close(serverFd);
}

void quit(int signo)
{
	isExit = true;
    stopCoroutines();
}

void *fun(void *arg){
    int fd = *(int*)arg;
    
    Coroutine *co = createCoroutine(acceptCoroutine, &fd);
    co->setPrio(1);
    yield;
    
    log(INFO, "exit sucess");
}

int main(int argc, char** argv){
    signal(SIGTERM,quit);
    
    int  serverFd;
    struct sockaddr_in  addr;
    
    if((serverFd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("create socket error: %s\n", strerror(errno));
        return 0;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(5566);
    if(bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        printf("bind socket error: %s)\n", strerror(errno));
        return 0;
    }
    if(listen(serverFd, 10) < 0){
        printf("listen socket error: %s\n", strerror(errno));
        return 0;
    }
    
    pthread_t t0,t1,t2;
    pthread_create(&t0, NULL, fun, &serverFd);
    pthread_create(&t1, NULL, fun, &serverFd);
    pthread_create(&t2, NULL, fun, &serverFd);
    
    pthread_join(t0, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    close(serverFd);
    
    log(INFO, "exit sucess");
}