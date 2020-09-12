#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)//创建信号量，第一个参数为允许几个线程恭喜信号量，一般0表示多线程共享，并为信号量赋初值,初始值是0
        {
            throw std::exception();//抛出异常
        }
    }
    sem(int num)//构造函数重载
    {
        if (sem_init(&m_sem, 0, num) != 0)//创建信号量，初始值为num
        {
            throw std::exception();
        }
    }
    ~sem()//析构
    {
        sem_destroy(&m_sem);//给信号量的值加1，当有线程阻塞在这个信号量上时，调用这个函数会使得其中一个线程不再阻塞，选择机制是由线程的调度策略决定的
    }
    bool wait()
    {
        return sem_wait(&m_sem) == 0;//从信号量的值减1，但它永远先等待该信号量为一个非零值才开始做减法
    }
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};
class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
