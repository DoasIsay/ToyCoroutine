/*
 * Copyright (c) 2020, xie wenwu <870585356@qq.com>
 * 
 * All rights reserved.
 */

#ifndef __MUTEX__
#define __MUTEX__

#include "queue.h"
#include "log.h"

class Coroutine;
extern __thread Coroutine *current;
extern int waitOnTimer(int timeout);

extern int getcid();
extern int gettid();

class Mutex:public Locker{
private:
    SpinLocker locker;
    volatile int ownCid;
    volatile int ownTid;
    
    SpinLocker gaurd;
    Queue<Coroutine*> waitQue;

public:
    void lock(){
        if(ownCid == getcid()) return;
        
        while(!trylock()){
            /*gaurd������֤ 1.trylock��ȡ��ʧ�� 2.push��waitQue������������ԭ���ԣ�ͬʱ����waitQue
              �ɷ�ֹ����һ���߳��е�Э��unlock�����ڵ�2������ǰ
              �����һ���߳��е�Э���ڵ�2������ǰ��unlock���ᵼ�µ�ǰЭ���޷������Ѳ�������
             */
            gaurd.lock();
            if(trylock()){
                gaurd.unlock();
                goto out;
            }
            /*������waitOnTimer���޸�Э�̵�״̬����Э���п��ܱ���һ���߳��е�Э��signal����
              ����ͬһ��Э�̱�����̵߳��ȣ�����Ӧ��push��waitQueǰ���޸�״̬
             */
            current->setState(SYNCING);
            waitQue.push(current);
            log(INFO, "lock fail ownCid %d ownTid %d, wait for it", ownCid, ownTid);
            gaurd.unlock();  
            
            if(waitOnTimer(-1) < 0)//���ź��жϷ��� 
                return;
        }
    out:
        ownCid = getcid();
        ownTid = gettid();
    }
    
    void unlock(){
        gaurd.lock();
        
        Coroutine *co = waitQue.pop();
        locker.unlock();
        ownCid = 0;
        ownTid = 0;

        gaurd.unlock();
        if(co == NULL) return;
        wakeup(co);
        log(INFO, "unlock wakeup %d", co->getcid());
    }

    bool trylock(){
        return locker.trylock();
    }

    bool state(){
        return locker.state();
    }

    int getOwnCid(){
        return ownCid;
    }

    int getOwnTid(){
        return ownTid;
    }
};

#endif
