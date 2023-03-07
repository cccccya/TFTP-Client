//编译命令：g++ TFTPClient.cpp -o TFTPClient.exe -lwsock32 -std=c++11
#include <cstdio>
#include <winsock2.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include <winbase.h>
#include <ctime>
#include <iostream>
#define LIMIT_T 30//超时时间
#define BUFLEN 1024//缓冲区大小
const char* MODE[2]={"netascii","octet"};//传输模式

//日志信息类
class Msg{
private:
    SYSTEMTIME nowtime;//操作时间
    double speed;//传输平均时间
    char resultMsg[64];//日志信息
    char filename[32];//文件名
    char mode[32];//传输模式
    bool result;//操作结果
public:
    void success(SYSTEMTIME t,double s,const char* f,const char* m){
        nowtime=t;
        speed=s;
        strcpy(filename,f);
        strcpy(mode,m);
        result=true;
    }
    void fail(SYSTEMTIME t,const char* msg,const char* f,const char* m){
        nowtime=t;
        strcpy(resultMsg,msg);
        strcpy(filename,f);
        strcpy(mode,m);
        result=false;
    }
    //格式化输出函数
    void print(FILE* fp){
        char buf[BUFLEN];
        int len  = sprintf(buf,"%4d/%02d/%02d %02d:%02d:%02d.%03d\n",nowtime.wYear,nowtime.wMonth,nowtime.wDay,nowtime.wHour,nowtime.wMinute, nowtime.wSecond,nowtime.wMilliseconds);  
        switch(result){
            case 0:
                sprintf(buf + len,"fail\tfilename:%s\tmode:%s\t%s\n\n\n",filename,mode,resultMsg);
                break;
            case 1:
                sprintf(buf + len,"success\tfilename:%s\tmode:%s\tspeed:%lfKB/s\n\n\n",filename,mode,speed);
                break;
        }
        printf("%s",buf);
        fwrite(buf,1,strlen(buf),fp);
    }
}msg;

bool WriteInfo(SOCKET clientSock,int port,const char* IP,int mode,const char* filename){
    SYSTEMTIME T;
    
    unsigned long Opt = 1;
    ioctlsocket(clientSock, FIONBIO, &Opt);//设置为非阻塞模式
    
    clock_t start,end;
    start = clock();//开始计时
    
    //按照不同传输模式打开文件
    FILE *fp;
    if(mode==0)fp=fopen(filename,"r");
    else fp=fopen(filename,"rb");
    //本地文件不存在
    if(fp==NULL){
        GetLocalTime(&T);
        msg.fail(T,"File not found",filename,MODE[mode]);
        return 0;
    }
    
    //配置服务器地址
    int nrc,len,filesz=0;
    sockaddr_in srvAddr,tempsrvAddr;int sz=sizeof(srvAddr);
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons(port);
    srvAddr.sin_addr.S_un.S_addr = inet_addr(IP);
    
    //定义发送缓冲区 接收缓冲区
    char sendBuf[BUFLEN],recv[BUFLEN];
    memset(sendBuf,0,BUFLEN);memset(recv,0,BUFLEN);
    
    //定义接收函数
    auto RECV = ([&](int expnum){
            struct timeval tv;
            fd_set readfds;//定义文件描述符集合
            for(int i = 0; i < 20; i++){
                FD_ZERO(&readfds);//清空文件描述符集合
                FD_SET(clientSock, &readfds);//将套接字加入文件描述符集合
                tv.tv_sec = LIMIT_T;//设置超时时间

                select(clientSock, &readfds, NULL, NULL, &tv);//监听套接字
                if(FD_ISSET(clientSock, &readfds)){//判断套接字是否可读
                    if ((nrc = recvfrom(clientSock, recv, BUFLEN, 0, (struct sockaddr*)&tempsrvAddr, &sz)) > 0){//接收数据
                        if(((unsigned char)recv[2] << 8) + (unsigned char)recv[3]==expnum)//判断是否为期望的数据包
                            return 1;
                    }
                }
                sendto(clientSock, sendBuf, len, 0, (struct sockaddr*)&srvAddr, sz);//重发数据包
            }
            return 0;
        }
    );

    //发送WRQ
    sprintf(sendBuf, "%c%c%s%c%s%c", 0, 2, filename, 0, MODE[mode], 0);
    len = 4 + strlen(filename) + strlen(MODE[mode]);
    nrc = sendto(clientSock, sendBuf, len, 0, (sockaddr*)&srvAddr, sz);
    if(nrc == SOCKET_ERROR){
        GetLocalTime(&T);
        msg.fail(T,"send WRQ fail",filename,MODE[mode]);
        return 0;
    }
    //接收ACK
    bool ret = RECV(0);
    //接收超时
    if(!ret){
        GetLocalTime(&T);
        msg.fail(T,"recv time out",filename,MODE[mode]);
        return 0;
    }
    //判断是否为错误报文
    if(recv[1]==5){
        GetLocalTime(&T);
        int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
        char buf[BUFLEN];
        sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
        msg.fail(T,buf,filename,MODE[mode]);
        return 0;
    }

    srvAddr = tempsrvAddr;//更新服务器地址
    while(1){
        int seq = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3] + 1;//计算下一个数据包的序号
        memset(sendBuf,0,BUFLEN);
        seq = (seq==65536?0:seq);//序号循环使用
        sendBuf[0] = 0;sendBuf[1] = 3;//数据包
        sendBuf[2] = (seq >> 8);sendBuf[3] = seq & 0xff;//序号
        len = fread(sendBuf+4, 1, 512, fp) + 4;//读取数据
        filesz += len - 4;//计算文件大小
        
        nrc = sendto(clientSock, sendBuf, len, 0, (sockaddr*)&srvAddr, sz);
        bool ret = RECV(seq);
        //接收超时
        if(!ret){
            GetLocalTime(&T);
            msg.fail(T,"recv time out",filename,MODE[mode]);
            return 0;
        }
        //判断是否为错误报文
        if(recv[1]==5){
            GetLocalTime(&T);
            int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
            char buf[BUFLEN];
            sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
            msg.fail(T,buf,filename,MODE[mode]);
            return 0;
        }

        //判断是否为最后一个数据包
        if(len<516){
            break;
        }
    }
    end = clock();//结束计时
    GetLocalTime(&T);//获取当前时间
    msg.success(T, filesz/((double)(end-start) / CLK_TCK)/1024, filename, MODE[mode]);//输出成功信息
    return 1;
}
bool ReadInfo(SOCKET clientSock,int port,const char* IP,int mode,const char* filename){
    SYSTEMTIME T;
    
    unsigned long Opt = 1;
    ioctlsocket(clientSock, FIONBIO, &Opt);//设置为非阻塞模式

    clock_t start,end;
    start = clock();//开始计时

    //按照不同传输模式打开文件
    FILE *fp;
    if(mode==0)fp=fopen(filename,"w");
    else fp=fopen(filename,"wb");

    //配置服务器地址
    int nrc,len,filesz=0;
    sockaddr_in srvAddr,tempsrvAddr;int sz=sizeof(srvAddr);
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons(port);
    srvAddr.sin_addr.S_un.S_addr = inet_addr(IP);

    //定义发送缓冲区 接收缓冲区
    char sendBuf[BUFLEN],recv[BUFLEN];
    memset(sendBuf,0,BUFLEN);memset(recv,0,BUFLEN);

    //定义接收函数
    auto RECV = ([&](int expnum){
            struct timeval tv;
            fd_set readfds;//设置文件描述符集合
            for(int i = 0; i < 20; i++){
                FD_ZERO(&readfds);//清空文件描述符集合
                FD_SET(clientSock, &readfds);//将套接字加入文件描述符集合
                tv.tv_sec = LIMIT_T;//设置超时时间

                select(clientSock, &readfds, NULL, NULL, &tv);//监听套接字
                if(FD_ISSET(clientSock, &readfds)){//判断套接字是否可读
                    if ((nrc = recvfrom(clientSock, recv, BUFLEN, 0, (struct sockaddr*)&tempsrvAddr, &sz)) > 0){//接收数据
                        if(((unsigned char)recv[2] << 8) + (unsigned char)recv[3]==expnum)//判断是否为期望的数据包
                            return 1;
                    }
                }
                sendto(clientSock, sendBuf, len, 0, (struct sockaddr*)&srvAddr, sz);//重发数据包
            }
            return 0;
        }
    );

    //发送RRQ请求
    sprintf(sendBuf, "%c%c%s%c%s%c", 0, 1, filename, 0, MODE[mode], 0);
    len = 4 + strlen(filename) + strlen(MODE[mode]);
    nrc = sendto(clientSock, sendBuf, len, 0, (sockaddr*)&srvAddr, sz);
    if(nrc == SOCKET_ERROR){
        GetLocalTime(&T);
        msg.fail(T,"send RRQ fail",filename,MODE[mode]);
        return 0;
    }
    //接收数据
    bool ret = RECV(1);
    if(!ret){
        GetLocalTime(&T);
        msg.fail(T,"recv time out",filename,MODE[mode]);
        return 0;
    }
    //判断是否为错误报文
    if(recv[1]==5){
        GetLocalTime(&T);
        int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
        char buf[BUFLEN];
        sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
        msg.fail(T,buf,filename,MODE[mode]);
        return 0;
    }
    srvAddr = tempsrvAddr;//更新服务器地址
    while(1){
        filesz += nrc - 4;//计算文件大小
        int seq = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];//获取数据包序号
        memcpy(sendBuf,recv,4);
        if(nrc<516){//判断是否为最后一个数据包
            fwrite(recv+4,1,nrc-4,fp);
            sendBuf[1] = 4;
            len = 4;
            sendto(clientSock, sendBuf, 4, 0, (sockaddr*)&srvAddr, sizeof(srvAddr));
            break;
        }
        else{//不是最后一个数据包
            fwrite(recv+4,1,512,fp);
            sendBuf[1] = 4;
            len = 4;
            sendto(clientSock, sendBuf, 4, 0, (sockaddr*)&srvAddr, sizeof(srvAddr));
            bool ret = RECV(seq + 1);
            //判断是否超时
            if(!ret){
                GetLocalTime(&T);
                msg.fail(T,"recv time out",filename,MODE[mode]);
                return 0;
            }
            //判断是否为错误报文
            if(recv[1]==5){
                GetLocalTime(&T);
                int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
                char buf[BUFLEN];
                sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
                msg.fail(T,buf,filename,MODE[mode]);
                return 0;
            }
        }
    }
    end = clock();//结束计时
    GetLocalTime(&T);//获取当前时间
    msg.success(T, filesz/((double)(end-start) / CLK_TCK)/1024, filename, MODE[mode]);//输出成功信息
    return 1;
}
int main(int argc, char* argv[]){
    //打开日志文件
    FILE* logfp;
    logfp = fopen("log.txt","w");
    fwrite("log\n",1,4,logfp);

    WSADATA wsaData;
    int nRC;
    SOCKET clientSock;
    u_long uNonBlock;
    //初始化 winsock
    nRC = WSAStartup(0x0101, &wsaData);
    if(nRC){
        printf("Client initialize winsock error!\n");
        return 0;
    }
    printf("Client's winsock initialized!\n");
    //创建 client socket
    clientSock = socket(AF_INET,SOCK_DGRAM,0);//UDP
    if(clientSock == INVALID_SOCKET){
        printf("Client create socket error!\n");
        WSACleanup();
        return 0;
    }
    int opt;
    bool flag=0;
    char desip[20],filename[20];
    printf("Client socket create OK!\n");
    printf("请输入服务器IP地址:");
    scanf("%s",desip);
    printf("\n请按照以下格式输入想执行的操作\n1 filename //以netascii模式上传名为filename的文件\n2 filename //以octet模式上传名为filename的文件\n3 filename //以netascii模式从服务器下载名为filename的文件\n4 filename //以octet模式从服务器下载名为filename的文件\n-1 //关闭client\n\n");
    while(1){
        scanf("%d",&opt);
        switch(opt){
            case 1://Write netascii
                scanf("%s",&filename);
                WriteInfo(clientSock,69,desip,0,filename);
                msg.print(logfp);
                break;
            case 2://Write octet
                scanf("%s",&filename);
                WriteInfo(clientSock,69,desip,1,filename);
                msg.print(logfp);
                break;
            case 3://Read netascii
                scanf("%s",&filename);
                ReadInfo(clientSock,69,desip,0,filename);
                msg.print(logfp);
                break;
            case 4://Read octet
                scanf("%s",&filename);
                ReadInfo(clientSock,69,desip,1,filename);
                msg.print(logfp);
                break;
            case -1:
                flag=1;
                break;
            default:
                break;
        }
        if(flag){
            break;
        }
    }
    closesocket(clientSock);//关闭socket
    WSACleanup();//清理winsock
}