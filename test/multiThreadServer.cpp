#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include "coroutine.h"
#include "log.h"
#include "syscall.h"

volatile bool isExit = false;

int socketHandleCoroutine(void *arg){
    char buf[19];
    int fd = *(int*)arg;
    
    //log(INFO, "start socketHandleCoroutine fd:%d", fd);
    
    while(!isExit){
        int ret = read(fd, buf, 19);
        if(ret == 0)
            break;
        else if(ret < 0){
            //log(ERROR, "fd:%d read error:%s\n", fd, strerror(errno));
            break;
        }
        //log(INFO, "fd:%d recv %s", fd, buf);
        
        ret = write(fd, buf, 19);
        if(ret <= 0){
            //log(ERROR, "fd:%d write error:%s\n", fd, strerror(errno));
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
            //log(INFO, "accept fd %d", *clientFd);
            createCoroutine(socketHandleCoroutine, (void*)clientFd);
        }else{
            log(ERROR, "accept error:%s", strerror(errno));
            break;
        }
    }
    
    close(serverFd);
    return 0;
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

void *fun1(void *arg){

    sysSleep(60);
    yield;
    
    log(INFO, "exit sucess");
}


int main(int argc, char** argv){
    signal(SIGINT, quit);
    signal(SIGTERM, quit);
    signal(SIGPIPE, SIG_IGN);
    
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
    if(listen(serverFd, 10000) < 0){
        printf("listen socket error: %s\n", strerror(errno));
        return 0;
    }
    
    pthread_t t0,t1,t2;
    pthread_create(&t0, NULL, fun , &serverFd);
    pthread_create(&t1, NULL, fun1, NULL);
    pthread_create(&t2, NULL, fun1, NULL);
    
    pthread_join(t0, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    close(serverFd);
    
    log(INFO, "exit sucess");
}
