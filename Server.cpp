#include<iostream>
#include"Server.h"

using namespace std;

Server::Server(){

    //initialize server ip and port
    serverAddr.sin_family = PF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    //initialize socket
    listener = 0;

    epfd = 0;
}

void Server::Init(){
    cout<<"Initializing server..."<<endl;

    //create socket
    listener= socket(PF_INET,SOCK_STREAM,0);
    if(listener<0) {
        perror("listener");
        exit(-1);
    }

    if(bind(listener,(struct sockaddr *)&serverAddr,sizeof(serverAddr))<0){
        perror("bind error");
        exit(-1);

    }
    //监听
    int ret = listen(listener, 5);
    if(ret < 0) {
        perror("listen error"); 
        exit(-1);
    }
    
    cout<<"Start to listen: "<<SERVER_IP<<endl;
    //在内核中创建事件表 epfd是一个句柄 
    epfd = epoll_create(EPOLL_SIZE);
    if(epfd<0){
        perror("epfd error");
        exit(-1);

    }
    
    addfd(epfd,listener,true);

}

void Server::Close(){

    //close socket
    close(listener);

    //close epoll
    close(epfd);

}


// 发送广播消息给所有客户端
int Server::SendBroadcastMessage(int clientfd)
{
    // buf[BUF_SIZE] 接收新消息
    // message[BUF_SIZE] 保存格式化的消息
    char recv_buf[BUF_SIZE];
    char send_buf[BUF_SIZE];
    Message msg;
    bzero(recv_buf, BUF_SIZE);

    cout<<"read from client(client ID="<<clientfd<<")"<<endl;
    int len = recv(clientfd,recv_buf,BUF_SIZE,0);

    //copy message from recv_buf to msg
    memset(&msg,0,sizeof(msg));
    memcpy(&msg,recv_buf,sizeof(msg));

    msg.fromID = clientfd;
    if(msg.content[0]=='\\' && isdigit(msg.content[1])){
        msg.type = 1;
        msg.toID = msg.content[1]-'0';
        memcpy(msg.content,msg.content+2,sizeof(msg.content));
    }
    else
    {
        msg.type = 0;
    }
    //if client close 
    if(len==0){
        close(clientfd);

        //remove client from clientlist
        clients_lists.remove(clientfd);
        cout << "ClientID = " << clientfd 
             << " closed.\n now there are " 
             << clients_lists.size()
             << " client in the char room"
             << endl;

    }

    else{ //broadcast to all client
        if(clients_lists.size()==1){

            //send welcome
            memcpy(&msg.content,CAUTION,sizeof(msg.content));
            bzero(send_buf,BUF_SIZE);
            memcpy(send_buf,&msg,sizeof(msg));
            send(clientfd,send_buf,sizeof(send_buf),0);
            return len;
        }

        char format_message[BUF_SIZE];
        //群聊
        if(msg.type==0){
            // 格式化发送的消息内容 #define SERVER_MESSAGE "ClientID %d say >> %s"
            sprintf(format_message, SERVER_MESSAGE, clientfd, msg.content);
            memcpy(msg.content,format_message,BUF_SIZE);
            // 遍历客户端列表依次发送消息，需要判断不要给来源客户端发
            list<int>::iterator it;
            for(it = clients_lists.begin(); it != clients_lists.end(); ++it) {
               if(*it != clientfd){
                    //把发送的结构体转换为字符串
                    bzero(send_buf, BUF_SIZE);
                    memcpy(send_buf,&msg,sizeof(msg));
                    if( send(*it,send_buf, sizeof(send_buf), 0) < 0 ) {
                        return -1;
                    }
               }
            }
        }
        //私聊
        if(msg.type==1){
            bool private_offline=true;
            sprintf(format_message, SERVER_PRIVATE_MESSAGE, clientfd, msg.content);
            memcpy(msg.content,format_message,BUF_SIZE);
            // 遍历客户端列表依次发送消息，需要判断不要给来源客户端发
            list<int>::iterator it;
            for(it = clients_lists.begin(); it != clients_lists.end(); ++it) {
               if(*it == msg.toID){
                    private_offline=false;
                    //把发送的结构体转换为字符串
                    bzero(send_buf, BUF_SIZE);
                    memcpy(send_buf,&msg,sizeof(msg));
                    if( send(*it,send_buf, sizeof(send_buf), 0) < 0 ) {
                        return -1;
                    }
               }
            }


    

        //如果私聊对象不在线
            if(private_offline){
                sprintf(format_message,SERVER_PRIVATE_ERROR_MESSAGE,msg.toID);
                memcpy(msg.content,format_message,BUF_SIZE);
                bzero(send_buf,BUF_SIZE);
                memcpy(send_buf,&msg,sizeof(msg));
                if(send(msg.fromID,send_buf,sizeof(send_buf),0)<0)
                    return -1;
            }
        }
    }
    return len;

}

// 启动服务端
void Server::Start() {
 
    // epoll 事件队列
    static struct epoll_event events[EPOLL_SIZE]; 
 
    // 初始化服务端
    Init();
 
    //主循环
    while(1)
    {
        //epoll_events_count表示就绪事件的数目
        int epoll_events_count = epoll_wait(epfd, events, EPOLL_SIZE, -1);
 
        if(epoll_events_count < 0) {
            perror("epoll failure");
            break;
        }
 
        cout << "epoll_events_count =\n" << epoll_events_count << endl;
 
        //处理这epoll_events_count个就绪事件
        for(int i = 0; i < epoll_events_count; ++i)
        {
            int sockfd = events[i].data.fd;
            //新用户连接
            if(sockfd == listener)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrLength = sizeof(struct sockaddr_in);
                int clientfd = accept( listener, ( struct sockaddr* )&client_address, &client_addrLength );
 
                cout << "client connection from: "
                     << inet_ntoa(client_address.sin_addr) << ":"
                     << ntohs(client_address.sin_port) << ", clientfd = "
                     << clientfd << endl;
 
                addfd(epfd, clientfd, true);
 
                // 服务端用list保存用户连接
                clients_lists.push_back(clientfd);
                cout << "Add new clientfd = " << clientfd << " to epoll" << endl;
                cout << "Now there are " << clients_lists.size() << " clients int the chat room" << endl;
 
                // 服务端发送欢迎信息  
                cout << "welcome message" << endl;                
                char message[BUF_SIZE];
                bzero(message, BUF_SIZE);
                sprintf(message, SERVER_WELCOME, clientfd);
                int ret = send(clientfd, message, BUF_SIZE, 0);
                if(ret < 0) {
                    perror("send error");
                    Close();
                    exit(-1);
                }
            }
            //处理用户发来的消息，并广播，使其他用户收到信息
            else {   
                int ret = SendBroadcastMessage(sockfd);
                if(ret < 0) {
                    perror("error");
                    Close();
                    exit(-1);
                }
            }
        }
    }
 
    // 关闭服务
    Close();
} 

