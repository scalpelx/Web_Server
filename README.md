# Web_Server
一个小型Web多线程服务器，基于HTTP/1.1协议，支持GET和POST请求，支持目录浏览，提供静态与动态服务

## 准备

    git clone git@github.com:scalpelx/Web_Server.git
    cd Web_Server
    make
    
## 使用

### 服务器端

    ./web_server port
    
### 客户端

可以用Telnet或者浏览器访问相关地址，如127.0.0.1:8080，动态服务的执行目录位于cgi

更多信息可以访问我的[网站](http://scalpel.vip/2017/05/24/webserver)