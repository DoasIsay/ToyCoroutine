/*
 * Copyright (c) 2020, xie wenwu <870585356@qq.com>
 * 
 * All rights reserved.
 */

#include <sys/syscall.h>
#include "scheduler.h"
#include "csignal.h"
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "syscall.h"

#define INTHZ 10

__thread Coroutine *last = NULL;

__thread Scheduler* Scheduler::instance = NULL;

Queue<Coroutine*, SpinLocker> Scheduler::RunQue;

Atomic Scheduler::Threads;

volatile int Scheduler::State = DEAD;

Scheduler::Scheduler(int max =1024){
    idx = 0;
    State = NEW;
    running = 0;
    
    maxEventSize = max;
    epfd = epollCreate(max);
    events = (epoll_event*)malloc(max*sizeof(epoll_event));
    
    groupid = syscall(__NR_gettid);
    
    #ifdef STACK_SEPARATE
    stack = (char*)malloc(SCH_STACK_SIZE);
    #endif
    Threads.inc();
}

#define setEvent(co, epfd, fd, type)\
        do{\
            co->setFd(fd);\
            co->setType(type);\
            co->setEpfd(epfd);\
        }while(0)

int Scheduler::wait(int fd, int type, int timeout){

    if(timeout > 0){
        current->setTimeout(timeout);
        current->setState(WAITING);
        timerQue.push(current);
    }

    if(timeout == 0){
        current->setState(RUNNABLE);
        addToRunQue(current);
    }else if(fd > 0){
        current->setState(WAITING);
        if(current->getEpfd() < 0){
            CorMap::Instance()->del(current->getcid());
            setEvent(current, epfd, fd, type);
            epollAddEvent(epfd, fd, type);
            CorMap::Instance()->set(fd, current);
        }else if(current->getType() != type){
            current->setType(type);
            epollModEvent(epfd, fd, type);
        }
    }
    
    schedule();
}

inline void Scheduler::wakeup(Coroutine *co){
    assert(co != NULL);
    
    co->setState(RUNNABLE);
    addToRunQue(co);
}

Coroutine* Scheduler::next(){
    Coroutine *co = NULL;
    
    for(; idx < SCH_PRIO_SIZE; idx++){
        if(runQue[running][idx].empty()) continue;
        co = runQue[running][idx].pop();
        runables--;
        return co;
    }
    idx = 0;
    running = !running;
    
    return co;
}

void Scheduler::relax(){
    if(State != RUNNING || CorMap::Instance()->empty()) return;
    
    int half = runables / 2;
    for(; half > 0 ; half--){
        Coroutine *co = next();
        if(co == NULL) break;
        
        if(co->getcid() == 0){
            addToRunQue(co);
            continue;
        }
        
        unbind(co);
        RunQue.push(co);
        log(INFO,"releax %d sys %d proc %d thread %d", co->getcid(), load.sysUsage, load.procUsage, load.threadUsage);
    }
    
    load.threadUsage -= 10;
}

inline Coroutine* Scheduler::stress(){
    if(RunQue.empty()) return NULL;
    
    Coroutine *co = RunQue.pop();
    if(co == NULL) return NULL;
    
    //����ո�relax��stress�������SYNCING״̬��Э�����ڵȴ�mutex/cond��������Ҫ��Ϊ�ȴ�mutex/cond�����Э������л�
    //SYNCING״̬��Э�̲��ܱ����̵߳���
    if(co->getGroupid() == groupid){
        if(co->getState() == SYNCING)
        return co;
    }else if(co->getState() == SYNCING){
        RunQue.push(co);
        return NULL;
    }
    
    bind(co);
    log(INFO,"stress %d sys %d proc %d thread %d", co->getcid(), load.sysUsage, load.procUsage, load.threadUsage);
    return co;
}

inline Coroutine *Scheduler::loadbalance(){
    if(Threads.get() > 1 &&
        State == RUNNING &&
        load.threadUsage > LOADBALANCE_FACTOR && 
        load.procUsage < Threads.get() * LOADBALANCE_FACTOR
    )
        relax();
    
    Coroutine *co = next();
    if(co == NULL &&
        (Threads.get() == 1 ||
        load.threadUsage < LOADBALANCE_FACTOR ||
        State != RUNNING)
    )
        co = stress();
    return co;
}

inline void Scheduler::switch_(Coroutine *co){    
    current = co;
    
    if(getcid() == 0) State = DEAD;
    else if(State == RUNNING) State = RUNNABLE;
    
    co->setState(RUNNING);
    restore(co->getContext(), (long)doSignal);        
}

inline void Scheduler::runProcess(){
    Coroutine *co = loadbalance();
    if(co == NULL) return;
    
    if(co->getcid() != 0 ||
        CorMap::Instance()->empty()){
        switch_(co);
    }else{
        addToRunQue(co);
    }
}

inline void Scheduler::timerProcess(){
    Coroutine *co = NULL;
    if(State != RUNNING || timerQue.empty()) return;
    
    co = timerQue.top();
    if(co->getTimeout() > time(NULL)) return;
    
    timerQue.pop();
    co->setTimeout(0);
    wakeup(co);
    
    return;
}

void Scheduler::eventProcess(){
    if(State != RUNNING) return;
    
    int firedEventSize = epollWait(epfd,
                                   events,
                                   maxEventSize,
                                   INTHZ);
    
    if(load.cacl());
        log(INFO, "threads %d sys %d proc %d thread %d", Threads.get(), load.sysUsage, load.procUsage, load.threadUsage);
    
    int nextEventIdx = 0;
    for(; nextEventIdx < firedEventSize; nextEventIdx++){
        Coroutine *co = NULL;
        co = (Coroutine*)events[nextEventIdx].data.ptr;
        wakeup(co);
    }
    
    return;
}

inline void Scheduler::signalProcess(){
    //�ڶ��̻߳����£�sigQueΪ���̰߳�ȫ�����첽�źŴ������е���stop()����sigQueʱҪ��ֹ�����߳�ͬʱ����sigQue
    //���ʹ���̰߳�ȫ��sigQue�п��ܵ�������
    if(State == STOPPING) return;
    
    Coroutine *co = sigQue.pop();
    if(co == NULL) return;
    
    assert(co->getState() != DEAD);
    co->setIntr(true);
    wakeup(co);
    return;
}

void Scheduler::schedule(){
    if(State == RUNNABLE || State == NEW) State = RUNNING;
    
    while(true){
        runProcess();
        timerProcess();
        eventProcess();
        signalProcess();
    }
}

void Scheduler::stop(){
    State = STOPPING;
    
    //����ȫ�ֹ�������е�Э�̻�û�а󶨵��������̣߳��ȴ��������̵߳��Ȱ���ЩЭ��
    if(!RunQue.empty()){
        log(WARN, "can`t stop immediately, the global RunQue size is %d, please stop it again laterly", RunQue.size());
        return;
    }
    
    //��ͣһ�¾����ܵȴ������������̼߳�⵽State == STOPPING
    sysSleep(1);
    
    while(true){
        Coroutine *co = CorMap::Instance()->next();
        
        if(co == NULL) break;
        //log(INFO,"stop coroutine %d",co->getcid());
        co->stop();
    }
    State = STOPPED;
}

Scheduler::~Scheduler(){
    //never happen
    close(epfd);
    free(events);
    #ifdef STACK_SEPARATE
    free(stack);
    #endif
    Threads.dec();
}

void addToRunQue(Coroutine *co){
    Scheduler::Instance()->RunQue.push(co);
    return;
}

int waitOnTimer(int timeout){
    return wait(-1, -1, timeout);
}

void wakeup(Coroutine *co){
    //����SYNCING״̬��Э�ָ̻����пɲ���ת��ΪRUNNABLE״̬
    if(co->getState() != SYNCING) co->setState(RUNNABLE);
    
    addToRunQue(co);
    return;
}

void stopCoroutines(){
    Scheduler::Instance()->stop();
    return;
}

void cexit(int status){
    current->setState(STOPPED);
    clear(current);
    switch(status){
        case -1:
        case  0:
        default:break;
    }
    
    if(last != NULL){
        assert(!isLinked(last));
        //now we can delete last coroutine safely on current coroutine stack
        delete last;
    }
    last = current;
    schedule();
}

void startCoroutine(){
    cexit(current->routine(current->arg));
}
