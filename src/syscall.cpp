 /*
 * Copyright (c) 2020, xie wenwu <870585356@qq.com>
 * 
 * All rights reserved.
 */

#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include "syscall.h"
#include "scheduler.h"

SysDup sysDup = (SysDup)dlsym(RTLD_NEXT, "dup");
SysRead sysRead	= (SysRead)dlsym(RTLD_NEXT, "read");
SysClose sysClose = (SysClose)dlsym(RTLD_NEXT, "close");
SysSleep sysSleep = (SysSleep)dlsym(RTLD_NEXT, "sleep");
SysWrite sysWrite = (SysWrite)dlsym(RTLD_NEXT, "write");
SysListen sysListen	= (SysListen)dlsym(RTLD_NEXT, "listen");
SysSocket sysSocket = (SysSocket)dlsym(RTLD_NEXT, "socket");
SysAccept sysAccept = (SysAccept)dlsym(RTLD_NEXT, "accept");
SysConnect sysConnect = (SysConnect)dlsym(RTLD_NEXT, "connect");


int setNoBlock(int fd, int block=1) {
    int flags;
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        log(ERROR, "fcntl get fd %d error %s", fd, strerror(errno));
        return -1;
    }

    if(block) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
       log(ERROR, "fcntl set fd %d error %s", fd, strerror(errno));
       return -1;
    }
    return 0;
}

int socket(int domain, int type, int protocol){
    int fd = sysSocket(domain, type, protocol);
    if(!current || fd  == -1)
       return fd;
    
    setNoBlock(fd);
    
    return fd;
}

int listen(int sockfd, int backlog){
    int ret = sysListen(sockfd, backlog);
    return ret;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    int ret = sysConnect(sockfd, addr, addrlen);
    if(!current)
        return ret;
    
    int error = -1;
    socklen_t len = sizeof(error);
    if(ret != -1)
        return ret;
    if(errno != EINPROGRESS)
        return ret;
    if(waitOnWrite(sockfd) < 0)
        return ret;
    
    ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    if(ret < 0)
        return ret;
    if(error != 0)
       errno = error;
    
    return ret;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen){
    int fd = sysAccept(sockfd, addr, addrlen);
    if(!current)
        return fd;
    
    if(fd != -1){
        setNoBlock(fd);
        return fd;
    }
    if(errno != EAGAIN)
        return -1;
    if(waitOnRead(sockfd) < 0)
        return -1;
    
    return accept(sockfd, addr, addrlen);
}

ssize_t read(int fd, void *buf, size_t count){
    int ret = sysRead(fd, buf, count);
    if(!current)
        return ret;

    if(ret != -1 || errno != EAGAIN)
        return ret;
    
    if(waitOnRead(fd) < 0)
        return ret;
    
    return read(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count){
    int ret = sysWrite(fd, buf, count);
    if(!current)
        return ret;
    
    if(ret != -1 || errno != EAGAIN)
        return ret;

    if(waitOnWrite(fd) < 0)
        return ret;
    
    return write(fd, buf, count);
}

int close(int fd){
    clear(current);
    
    return sysClose(fd);
}

unsigned int sleep(unsigned int seconds){
    int ret;
    if(!current)
        return sysSleep(seconds);
    else
        ret = waitOnTimer((seconds + time(NULL))*1000);
    
    return ret;
}

int dup(int oldfd){
    int fd = sysDup(oldfd);
    if(!current)
       return fd;
    
    if(fd != -1){
        setNoBlock(fd);
    }
    
    return fd;
}

