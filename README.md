# WebServer
# 项目介绍
linux下C++开发的多并发web服务器，支持多个浏览器同时向服务器发送HTTP请求，以及定期处理非活跃连接
# 主要技术
线程池；I/O多路复用；多线程实现事务并发处理；线程同步；状态机解析HTTP请求
# 运行
## 修改文件路径
将http_conn.cpp文件下的doc_root改为本地需要访问的html文件路径
## 编译链接
在命令行输入g++ *.cpp -pthread
## 运行
./a.out port_number

其中，port_number为输入的端口号，如10000
## 访问
打开浏览器，输入html地址http://192.168.16.132:10000/index.html

其中，192.168.16.132为linux服务器的ip地址
