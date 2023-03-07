# TFTP-Client
华中科技大学网络空间安全学院2020级计实验
## 实验要求
完成一个TFTP协议客户端程序，实现一下要求：
1. 严格按照TFTP协议与标准TFTP服务器通信；
2.	能够实现两种不同的传输模式netascii和octet；
3.	能够将文件上传到TFTP服务器；
4. 能够从TFTP服务器下载指定文件；
5.	能够向用户展现文件操作的结果：文件传输成功/传输失败；
6.	针对传输失败的文件，能够提示失败的具体原因；
7.	能够显示文件上传与下载的吞吐量；
8.	能够记录日志，对于用户操作、传输成功，传输失败，超时重传等行为记录日志；
## 系统结构设计
![image](https://user-images.githubusercontent.com/77919385/223397774-22fecf4f-bcc6-4359-bd32-e1141c7c39a5.png)
## 编译命令
g++ TFTPClient.cpp -o TFTPClient.exe -lwsock32 -std=c++11
