//�������g++ TFTPClient.cpp -o TFTPClient.exe -lwsock32 -std=c++11
#include <cstdio>
#include <winsock2.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include <winbase.h>
#include <ctime>
#include <iostream>
#define LIMIT_T 30//��ʱʱ��
#define BUFLEN 1024//��������С
const char* MODE[2]={"netascii","octet"};//����ģʽ

//��־��Ϣ��
class Msg{
private:
    SYSTEMTIME nowtime;//����ʱ��
    double speed;//����ƽ��ʱ��
    char resultMsg[64];//��־��Ϣ
    char filename[32];//�ļ���
    char mode[32];//����ģʽ
    bool result;//�������
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
    //��ʽ���������
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
    ioctlsocket(clientSock, FIONBIO, &Opt);//����Ϊ������ģʽ
    
    clock_t start,end;
    start = clock();//��ʼ��ʱ
    
    //���ղ�ͬ����ģʽ���ļ�
    FILE *fp;
    if(mode==0)fp=fopen(filename,"r");
    else fp=fopen(filename,"rb");
    //�����ļ�������
    if(fp==NULL){
        GetLocalTime(&T);
        msg.fail(T,"File not found",filename,MODE[mode]);
        return 0;
    }
    
    //���÷�������ַ
    int nrc,len,filesz=0;
    sockaddr_in srvAddr,tempsrvAddr;int sz=sizeof(srvAddr);
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons(port);
    srvAddr.sin_addr.S_un.S_addr = inet_addr(IP);
    
    //���巢�ͻ����� ���ջ�����
    char sendBuf[BUFLEN],recv[BUFLEN];
    memset(sendBuf,0,BUFLEN);memset(recv,0,BUFLEN);
    
    //������պ���
    auto RECV = ([&](int expnum){
            struct timeval tv;
            fd_set readfds;//�����ļ�����������
            for(int i = 0; i < 20; i++){
                FD_ZERO(&readfds);//����ļ�����������
                FD_SET(clientSock, &readfds);//���׽��ּ����ļ�����������
                tv.tv_sec = LIMIT_T;//���ó�ʱʱ��

                select(clientSock, &readfds, NULL, NULL, &tv);//�����׽���
                if(FD_ISSET(clientSock, &readfds)){//�ж��׽����Ƿ�ɶ�
                    if ((nrc = recvfrom(clientSock, recv, BUFLEN, 0, (struct sockaddr*)&tempsrvAddr, &sz)) > 0){//��������
                        if(((unsigned char)recv[2] << 8) + (unsigned char)recv[3]==expnum)//�ж��Ƿ�Ϊ���������ݰ�
                            return 1;
                    }
                }
                sendto(clientSock, sendBuf, len, 0, (struct sockaddr*)&srvAddr, sz);//�ط����ݰ�
            }
            return 0;
        }
    );

    //����WRQ
    sprintf(sendBuf, "%c%c%s%c%s%c", 0, 2, filename, 0, MODE[mode], 0);
    len = 4 + strlen(filename) + strlen(MODE[mode]);
    nrc = sendto(clientSock, sendBuf, len, 0, (sockaddr*)&srvAddr, sz);
    if(nrc == SOCKET_ERROR){
        GetLocalTime(&T);
        msg.fail(T,"send WRQ fail",filename,MODE[mode]);
        return 0;
    }
    //����ACK
    bool ret = RECV(0);
    //���ճ�ʱ
    if(!ret){
        GetLocalTime(&T);
        msg.fail(T,"recv time out",filename,MODE[mode]);
        return 0;
    }
    //�ж��Ƿ�Ϊ������
    if(recv[1]==5){
        GetLocalTime(&T);
        int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
        char buf[BUFLEN];
        sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
        msg.fail(T,buf,filename,MODE[mode]);
        return 0;
    }

    srvAddr = tempsrvAddr;//���·�������ַ
    while(1){
        int seq = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3] + 1;//������һ�����ݰ������
        memset(sendBuf,0,BUFLEN);
        seq = (seq==65536?0:seq);//���ѭ��ʹ��
        sendBuf[0] = 0;sendBuf[1] = 3;//���ݰ�
        sendBuf[2] = (seq >> 8);sendBuf[3] = seq & 0xff;//���
        len = fread(sendBuf+4, 1, 512, fp) + 4;//��ȡ����
        filesz += len - 4;//�����ļ���С
        
        nrc = sendto(clientSock, sendBuf, len, 0, (sockaddr*)&srvAddr, sz);
        bool ret = RECV(seq);
        //���ճ�ʱ
        if(!ret){
            GetLocalTime(&T);
            msg.fail(T,"recv time out",filename,MODE[mode]);
            return 0;
        }
        //�ж��Ƿ�Ϊ������
        if(recv[1]==5){
            GetLocalTime(&T);
            int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
            char buf[BUFLEN];
            sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
            msg.fail(T,buf,filename,MODE[mode]);
            return 0;
        }

        //�ж��Ƿ�Ϊ���һ�����ݰ�
        if(len<516){
            break;
        }
    }
    end = clock();//������ʱ
    GetLocalTime(&T);//��ȡ��ǰʱ��
    msg.success(T, filesz/((double)(end-start) / CLK_TCK)/1024, filename, MODE[mode]);//����ɹ���Ϣ
    return 1;
}
bool ReadInfo(SOCKET clientSock,int port,const char* IP,int mode,const char* filename){
    SYSTEMTIME T;
    
    unsigned long Opt = 1;
    ioctlsocket(clientSock, FIONBIO, &Opt);//����Ϊ������ģʽ

    clock_t start,end;
    start = clock();//��ʼ��ʱ

    //���ղ�ͬ����ģʽ���ļ�
    FILE *fp;
    if(mode==0)fp=fopen(filename,"w");
    else fp=fopen(filename,"wb");

    //���÷�������ַ
    int nrc,len,filesz=0;
    sockaddr_in srvAddr,tempsrvAddr;int sz=sizeof(srvAddr);
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_port = htons(port);
    srvAddr.sin_addr.S_un.S_addr = inet_addr(IP);

    //���巢�ͻ����� ���ջ�����
    char sendBuf[BUFLEN],recv[BUFLEN];
    memset(sendBuf,0,BUFLEN);memset(recv,0,BUFLEN);

    //������պ���
    auto RECV = ([&](int expnum){
            struct timeval tv;
            fd_set readfds;//�����ļ�����������
            for(int i = 0; i < 20; i++){
                FD_ZERO(&readfds);//����ļ�����������
                FD_SET(clientSock, &readfds);//���׽��ּ����ļ�����������
                tv.tv_sec = LIMIT_T;//���ó�ʱʱ��

                select(clientSock, &readfds, NULL, NULL, &tv);//�����׽���
                if(FD_ISSET(clientSock, &readfds)){//�ж��׽����Ƿ�ɶ�
                    if ((nrc = recvfrom(clientSock, recv, BUFLEN, 0, (struct sockaddr*)&tempsrvAddr, &sz)) > 0){//��������
                        if(((unsigned char)recv[2] << 8) + (unsigned char)recv[3]==expnum)//�ж��Ƿ�Ϊ���������ݰ�
                            return 1;
                    }
                }
                sendto(clientSock, sendBuf, len, 0, (struct sockaddr*)&srvAddr, sz);//�ط����ݰ�
            }
            return 0;
        }
    );

    //����RRQ����
    sprintf(sendBuf, "%c%c%s%c%s%c", 0, 1, filename, 0, MODE[mode], 0);
    len = 4 + strlen(filename) + strlen(MODE[mode]);
    nrc = sendto(clientSock, sendBuf, len, 0, (sockaddr*)&srvAddr, sz);
    if(nrc == SOCKET_ERROR){
        GetLocalTime(&T);
        msg.fail(T,"send RRQ fail",filename,MODE[mode]);
        return 0;
    }
    //��������
    bool ret = RECV(1);
    if(!ret){
        GetLocalTime(&T);
        msg.fail(T,"recv time out",filename,MODE[mode]);
        return 0;
    }
    //�ж��Ƿ�Ϊ������
    if(recv[1]==5){
        GetLocalTime(&T);
        int errcode = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];
        char buf[BUFLEN];
        sprintf(buf,"error code:%d error message:%s",errcode,recv+4);
        msg.fail(T,buf,filename,MODE[mode]);
        return 0;
    }
    srvAddr = tempsrvAddr;//���·�������ַ
    while(1){
        filesz += nrc - 4;//�����ļ���С
        int seq = ((unsigned char)recv[2] << 8) + (unsigned char)recv[3];//��ȡ���ݰ����
        memcpy(sendBuf,recv,4);
        if(nrc<516){//�ж��Ƿ�Ϊ���һ�����ݰ�
            fwrite(recv+4,1,nrc-4,fp);
            sendBuf[1] = 4;
            len = 4;
            sendto(clientSock, sendBuf, 4, 0, (sockaddr*)&srvAddr, sizeof(srvAddr));
            break;
        }
        else{//�������һ�����ݰ�
            fwrite(recv+4,1,512,fp);
            sendBuf[1] = 4;
            len = 4;
            sendto(clientSock, sendBuf, 4, 0, (sockaddr*)&srvAddr, sizeof(srvAddr));
            bool ret = RECV(seq + 1);
            //�ж��Ƿ�ʱ
            if(!ret){
                GetLocalTime(&T);
                msg.fail(T,"recv time out",filename,MODE[mode]);
                return 0;
            }
            //�ж��Ƿ�Ϊ������
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
    end = clock();//������ʱ
    GetLocalTime(&T);//��ȡ��ǰʱ��
    msg.success(T, filesz/((double)(end-start) / CLK_TCK)/1024, filename, MODE[mode]);//����ɹ���Ϣ
    return 1;
}
int main(int argc, char* argv[]){
    //����־�ļ�
    FILE* logfp;
    logfp = fopen("log.txt","w");
    fwrite("log\n",1,4,logfp);

    WSADATA wsaData;
    int nRC;
    SOCKET clientSock;
    u_long uNonBlock;
    //��ʼ�� winsock
    nRC = WSAStartup(0x0101, &wsaData);
    if(nRC){
        printf("Client initialize winsock error!\n");
        return 0;
    }
    printf("Client's winsock initialized!\n");
    //���� client socket
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
    printf("�����������IP��ַ:");
    scanf("%s",desip);
    printf("\n�밴�����¸�ʽ������ִ�еĲ���\n1 filename //��netasciiģʽ�ϴ���Ϊfilename���ļ�\n2 filename //��octetģʽ�ϴ���Ϊfilename���ļ�\n3 filename //��netasciiģʽ�ӷ�����������Ϊfilename���ļ�\n4 filename //��octetģʽ�ӷ�����������Ϊfilename���ļ�\n-1 //�ر�client\n\n");
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
    closesocket(clientSock);//�ر�socket
    WSACleanup();//����winsock
}