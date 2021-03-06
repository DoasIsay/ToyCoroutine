/*
 * Copyright (c) 2020, xie wenwu <870585356@qq.com>
 * 
 * All rights reserved.
 */

#ifndef __SCHEDULER__
#define __SCHEDULER__

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <queue>
#include "coroutine.h"
#include "context.h"
#include "cormap.h"
#include "queue.h"
#include "epoll.h"
#include "log.h"
#include "load.h"
#include "sync/atomic.h"

static inline void clear(Coroutine *co){
    if(!co) return;
    
    CorMap::Instance()->del(co->getcid());
    if(co->getEpfd() > 0){
        epollDelEvent(co->getEpfd(), co->getFd(), co->getType());
        co->setEpfd(-1);
    }
}

class Scheduler{
private:
    friend void addToRunQue(Coroutine *co);
    
    int idx;
    int running;
    int runables;

    Load load;
    pid_t groupid;
    static Atomic Threads;
    volatile static int State;

    int epfd;
    int maxEventSize;
    epoll_event *events;
    
    Queue<Coroutine*> sigQue;
    static Queue<Coroutine*, SpinLocker> RunQue;
    Queue<Coroutine*> runQue[2][SCH_PRIO_SIZE];
    std::priority_queue<Coroutine*, std::vector<Coroutine*>, compare> timerQue;
    
    Scheduler(int max);
    Scheduler() = delete;
    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler&) = delete;

#ifdef STACK_SEPARATE
    char *stack;
#endif

    static __thread Scheduler *instance;
    
public:
    static Scheduler* Instance(){
        if(!instance)
            instance = new Scheduler(1024);
        return instance;
    }

    void stop();

    void switch_(Coroutine *co);
    
    void wakeup(Coroutine *co);
    
    int  wait(int fd, int type, int timeout);
   
    void schedule();
    
    void runProcess();
    
    void timerProcess();
    
    void eventProcess();
    
    void signalProcess();

    void relax();
    
    Coroutine* stress();
    
    Coroutine* loadbalance();
    
    pid_t getGrouid(){
        return groupid;
    }

    #ifdef STACK_SEPARATE
    char *getStack(){
        return stack;
    }
    #endif
    
    Coroutine* next();
    
    void addToRunQue(Coroutine *co){
        runables++;
        runQue[!running][co->getPrio()].push(co);
    }

    void addToSigQue(Coroutine *co){
       sigQue.push(co);
    }
    
    void bind(Coroutine* co){
        co->setGroupid(groupid);
        co->setSched(this);       
    }
    
    void unbind(Coroutine* co){
        if(co->getEpfd() != -1) clear(co);
        co->setSched(NULL);
    }

    ~Scheduler();
};

void addToRunQue(Coroutine *co);

void stopCoroutines();

extern __thread Coroutine *last;

static inline int wait(int fd, int type, int timeout){
    assert(current != NULL);
    if(save(current->getContext())){
        if(!current->getIntr()) return 1;
        //被信号中断返回
        current->setIntr(false);
        return -1;
    }

    //启用多线程负载均衡调度后一定要开启栈分离，否则会出现同一个协程栈被多个线程并发使用的情况
#ifdef STACK_SEPARATE
    long sp = (long)(Scheduler::Instance()->getStack() + SCH_STACK_SIZE);
    //switch coroutine stack to scheduler stack
#ifdef __i386__
    asm("movl %0,%%esp;"::"m"(sp));
#elif __x86_64__
    asm("movq %0,%%rsp;"::"m"(sp));
#endif
#endif
    
    Scheduler::Instance()->wait(fd, type, timeout);
}

int waitOnTimer(int timeout);

static inline int waitOnRead(int fd){
    return wait(fd, READABLE, -1);
}

static inline int waitOnWrite(int fd){
    return wait(fd, WRITEABLE, -1);
}

void wakeup(Coroutine *co);

static inline void schedule(){
#ifdef STACK_SEPARATE
    long sp = (long)(Scheduler::Instance()->getStack() + SCH_STACK_SIZE);
    //switch coroutine stack to scheduler stack
#ifdef __i386__
    asm("movl %0,%%esp;"::"m"(sp));
#elif __x86_64__
    asm("movq %0,%%rsp;"::"m"(sp));
#endif
#endif

    Scheduler::Instance()->schedule();
}

void cexit(int status);
void startCoroutine();

#endif

