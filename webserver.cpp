#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];//存放http_conn对象队列

    //root文件夹路径
    char server_path[200];
    //getcwd用来获得当前工作目录的绝对路径名，放到buf中
    getcwd(server_path, 200);//将当前的工作目录绝对路径复制到参数中，第二个参数为第一个buf的大小
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);//拷贝
    strcat(m_root, root);//连接，现在的成员变量应该是  当前文件夹路径+/root

    //定时器
    users_timer = new client_data[MAX_FD];//存放client_data的队列
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, int sqlverify,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;//端口号
    m_user = user;//数据库用户名
    m_passWord = passWord;//数据库密码
    m_databaseName = databaseName;//数据库名称
    m_sql_num = sql_num;//数据库连接池，8
    m_thread_num = thread_num;//线程池线程数量,8
    m_log_write = log_write;//日志写入方式，同步0
    m_SQLVerify = sqlverify;//数据库校验方式，同步0
    m_OPT_LINGER = opt_linger;//是否优雅关闭，否0
    m_TRIGMode = trigmode;//触发模式，LT0
    m_close_log = close_log;//是否关闭日志，否0
    m_actormodel = actor_model;//触发模式，proactor0
}

void WebServer::log_write()
{
    if (0 == m_close_log)//0不关闭日志
    {
        //初始化日志
        if (1 == m_log_write)//日志写入方式
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);//异步写入日志
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);//同步写入日志
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();//单例模式创建对象
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    if (0 == m_SQLVerify)//如果是同步的检验方式
        users->initmysql_result(m_connPool);
    else if (1 == m_SQLVerify)
        users->initresultFile(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

//套接子创建、绑定、监听
void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);//IPV4 、TCP连接、flags
    
    assert(m_listenfd >= 0);
/*
struct linger {
     int l_onoff;   //0 = off, nozero = on 
     int l_linger;   //linger time 
};
*/
    //优雅关闭连接
    if (0 == m_OPT_LINGER)//不优雅关闭
    {
        struct linger tmp = {0, 1};
        /*
         OPT_LINstruct linger
        {
        int l_onoff;		//指定了套接字是否还需要继续保持打开状态一段时间，通过这段时间将未发送的数据进行发送，该参数为0时表示cloaseaocket（）函数之后套接字马上关闭，
        未发送的数据将不会被发送，该参数的值为非0时，表示cloasesocket()函数调用后套接字先保持一段时间再关闭，第二个参数指定了等待的时间，单位是s，只有第一个参数为非0时才有效
        int l_linger;		// Time to linger.  
        }; * /
        /*设置 l_onoff为0，则该选项关闭，l_linger的值被忽略，等于内核缺省情况，close调用会立即返回给调用者，如果可能将会传输任何未发送的数据；*/
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)//优雅关闭
    {
        struct linger tmp = {1, 1};
        /*设置 l_onoff 为非0，l_linger为非0，当套接口关闭时内核将拖延一段时间（由l_linger决定）。
        如果为阻塞的close会等待一段事件，直到TCP发送网所有的残留数据并得到确认。
        如果为非阻塞的close将自己返回，此时要通过返回值来判断数据是否发送完毕。*/
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

// 设置服务器IP和Port，和监听描述副绑定，是具体的ipv4地址所以使用的是_in
//主机地址的都是主机字节序，也叫小端续，高位高地址，地位低地址；在发送中要求是大端序，所以要使用htonl转换为小端序，即网络字节序。IP地址使用htonl长整形转换，端口号使用ntons短整形转换
    int ret = 0;
    struct sockaddr_in address;//专门用于IPV4的地址家族，包含了服务器的各种信息
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;//地址家族，ipv4
    address.sin_addr.s_addr = htonl(INADDR_ANY);//主机字节序的IP地址，一个整型宏，本机所有的IP地址转换为网络字节序整型
    address.sin_port = htons(m_port);//主机字节序的端口号，服务器主机端口号转换成网络字节序短整型

    int flag = 1;
    //在TIME_WAIT状态的时候，此时是不能新建连接的，但是通过SO_REUSERADDRK可以对这个套接字进行重用
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));// 对套接字进行设置，消除bind时"Address already in use"错误
    //将端口号绑定到之前创建的未命名套接字上
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));//参数分别为监听描述符、本机地址，和长度
    assert(ret >= 0);
    //创建监听队列，存放待处理的客户连接。这个监听队列就是未连接队列，凡是收到SYN的都先被放入这个队列中
    ret = listen(m_listenfd, 5); // 开始监听，最大等待队列（未决连接数）长为LISTENQ，即最多同时可以监听到5个客户端。客户端到来的时候，放入未决连接队列中。
    assert(ret >= 0);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;

    utils.init(timer_lst, TIMESLOT);//初始化，设置成员变量

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];//用来储存活跃描述符
    m_epollfd = epoll_create(5);//创建m_epollfd
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_TRIGMode);//将刚才创建的listenfd加入到epollfd中
    http_conn::m_epollfd = m_epollfd;

    //进程间通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);//创建套接字对(25,26)，用于在父子进程间通信
    /*当参数指定为SOCK_STREAM时，得到的结果称为流管道，它与一般管道的区别是留管道是全双工的，即两个描述符即可读有可写
    第四个参数用于保存创建的套接字对；
    socketpair函数建立一对匿名的已连接的套接字
    ，建立的两个套接字描述符会放在sv[0]和sv[1]中。
    既可以从sv[0]写入sv[1]读出，又可以从sv[1]读入sv[0]写出，如果没有写入就读出则会生阻塞。用途：用来创建全双工通道，不过只局限于父子进程之间。
    */
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);//将26设置为非阻塞状态,通过fcntl改变文件描述符状态
    utils.addfd(m_epollfd, m_pipefd[0], false, m_TRIGMode);//将25加入到epoll中，监听26是否有内容传过来，如果传过来25会处于一个活跃状态

    /*当服务器close一个连接时，若client端接着发数据。根据TCP 协议的规定，会收到一个RST响应，client再往这个服务器发送数据时，
    系统会发出一个SIGPIPE信号给进程，告诉进程这个连接已经断开了，不要再写了。  　　
    根据信号的默认处理规则SIGPIPE信号的默认执行动作是terminate(终止、退出),所以client会退出。若不想客户端退出可以把SIGPIPE设为SIG_IGN  　*/
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)//传入的是accept和客户端地址
{
    users[connfd].init(connfd, client_address, m_root, m_SQLVerify, m_TRIGMode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;//新建定时器
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    timer_lst.add_timer(timer);//放入定时器队列
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;//过期时间向后延迟
    timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_TRIGMode)//0，LT水平触发，只要完成队列里面有信息就一直会触发，所以不同循环也不会丢失客户端的信息
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);//accept得到connfd，并同时将客户端的地址写入传入的参数
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }

    else//ET上升沿触发
    {
        while (1)//注意！！！如果是ET的话accept必须也要设置为while模式，直到accept发生错误。当多个连接同事到达，TCP就绪队列会积累多个就绪连接
        //由于是ET模式，epoll只会通知一次，需要循环地从完成队列中不断地取出信息进行accept。否则的话可能会丢失客户端的信息。
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);//accept的作用是从完成队列中取出客户端的信息建立连接
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;//如果是ET返回false
    }
    return true;//LT返回true
}

bool WebServer::dealwithsignal(bool timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];//接受信号的buff
    //recv的作用就是将fd中的数据读到缓冲区中
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);//将数据接收到signal buff中
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM://获得SIGALRM
            {
                timeout = true;//超时标志
                break;
            }
            case SIGTERM:
            {
                stop_server = true;//服务器停止
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;//提取出此套接字对应的定时器中的定时器

    //reactor，如果是actor，则将事件添加到请求队列，唤醒子线程来负责读写的事件并且解析请求
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);//将过期时间延后并且调整在链表里面的位置
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);//传入的第一个参数将作为request，第二个参数为state

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor，IO事件就绪时候，先将读写事件使用主线程完成，然后唤醒子线程来执行之后的请求解析等等
        if (users[sockfd].read_once())//循环得将内容读完，读到了m_read_buf 中
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));//打印log

            //若监测到读事件，将该事件放入请求队列（锁和信号量）
            m_pool->append_p(users + sockfd);//任务resquest由users+sockfd组合而成，放到任务队列中，post似的信号量加1，会立即使得一个阻塞线程去执行这个任务

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;//超时标志
    bool stop_server = false;//停止服务标志

    while (!stop_server)//一直循环
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);//等待epoll返回活跃事件
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;//取出活跃的fd

            //处理新到的客户连接。对listen和acceptfd的理解：一个服务器只有一个listenfd，因为只有一个端口。但是这个端口下有两个队列：未决连接队列和已决连接队列。
            //accept的作用就是从已决连接队列中取出TCP连接并且为这个客户端连接专门 的acceptfd，之后就用这个fd的活跃程度来代表这个客户端的各种请求事件。
            //所以listenfd和acceptfd是一对多的关系
            if (sockfd == m_listenfd)//如果是监听事件，即这个客户端刚刚完成了三次握手
            {
                bool flag = dealclinetdata();
                if (false == flag)//如果是ET则返回false，则退出此次for循环？？？
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))//发生了某些错误事件，即events是这些标志的某一个
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //如果是之前设定的进程通信的套接字收到了信号，处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))//如果是
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    continue;
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)//EPOLLIN事件，即读事件
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)//EPOLLOUT事件，即写时间
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)//如果是超时问题
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}