#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理，任务队列的信号量
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换，reactor,proactor
};
template <typename T>//函数模板
//初始化成员列表
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];//根据线程数量创建线程id数组m_threads
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)//创建线程，回调函数work
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))//即主线程与子线程分离，子线程结束后，资源自动回收
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
//模板偏特化
template <typename T>//给request设置为模板
bool threadpool<T>::append(T *request, int state)//reactor的append
{
    m_queuelocker.lock();//对队列加锁
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;//提取出请求的状态
    m_workqueue.push_back(request);//将请求放到工作队列中
    m_queuelocker.unlock();//队列解锁
    m_queuestat.post();//信号量加1
    cout<<"信号量加1"<<endl;
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)//proactor时的append
{
    m_queuelocker.lock();//锁
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);//放入队列
    m_queuelocker.unlock();
    m_queuestat.post();//信号量+1
    cout<<"信号量加1"<<endl;
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;//利用传入的参数建立线程池对象
    pool->run();//执行run函数
    return pool;
}

template <typename T>
void threadpool<T>::run()//线程的回调函数，在创建线程的时候就指定了，但是由于某些原因，线程可能并不能直接执行完这个函数
{
    while (true)//这是一个总循环，所以所有的线程都会不停重复这个等待这个信号量大于0的过程，只要别的地方post了这里就会立即运行一次
    {
        cout<<"线程等待信号量"<<endl;
        m_queuestat.wait();/*信号量减1，一开始建立线程的时候每个线程都会被阻塞到这个wait的地方，因为此时的信号量是0不能再减少了
        当别的地方把任务加到了任务队列中，post会使信号量加1，这里被阻塞的线程会获得执行权，继续往下执行去执行队列里面的任务，同时信号量减1，其他的线程继续被阻塞
        post的作用不是唤醒线程，而是pos会把信号量加1，这个wait会自动检测到信号量的之从而执行任务，这是一个完成的流程。当线程执行完这个任务之后由会继续回到这里阻塞。
        至于阻塞的线程来执行任务的问题，是靠内部的竞争机制实现的。
        线程等待信号量
        线程等待信号量
        线程等待信号量
        线程等待信号量
        线程等待信号量
        线程等待信号量
        线程等待信号量
        线程等待信号量//一开始的时候，8个线程都被阻塞到了这里
        信号量加1  //当有任务被放到了队列里面，信号量加1
        信号量减1  //wait的线程会有一个直接通过wait开始执行任务
        线程等待信号量  //这个任务执行的速度太快了，在一个任务到来之前就完成了，由于是个循环，所以又回去等待了
        信号量加1  //又有一个任务加入到了任务队列中
        信号量减1
        线程等待信号量//这里的信号量用来控制任务队列的，任务队列中未处理的任务就是信号量，任务每次来一个就会post加1，加1立即拍一个线程去完成这个任务
        */
        cout<<"信号量减1"<<endl;
        m_queuelocker.lock();//队列加锁
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();//取出队列的任务
        m_workqueue.pop_front();//弹出
        m_queuelocker.unlock();//任务队列解锁
        if (!request)
            continue;
        if (1 == m_actor_model)//并发模式为reactor
        {
            if (0 == request->m_state)
            {
                if (request->read_once())//全部读出
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();//真正的处理请求部分
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else//并发模式为proactor
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
