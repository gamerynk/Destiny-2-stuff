#include <WinSock2.h>
#include <WS2tcpip.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>
#include <vector>
bool synBlackhole=false;
int WSAAPI test_connect(SOCKET value,const sockaddr* address,int size){
    if(synBlackhole){WSASetLastError(WSAEWOULDBLOCK);return SOCKET_ERROR;}
    return ::connect(value,address,size);
}
int WSAAPI test_select(int ignored,fd_set* read,fd_set* write,fd_set* error,const timeval* timeout){
    if(synBlackhole){Sleep(static_cast<DWORD>(timeout->tv_sec*1000+timeout->tv_usec/1000));return 0;}
    return ::select(ignored,read,write,error,timeout);
}
#define connect test_connect
#define select test_select
#include "../dawn/src/client/hooks/external_server/external_server_transport.cpp"
#undef connect
#undef select
namespace dawn::core::settings {
Settings testSettings{};
const Settings& get() noexcept { return testSettings; }
}
namespace transport=dawn::client::hooks::external_server;
int failures=0;
void check(bool value,const char* name){if(!value){++failures;std::printf("FAIL %s\n",name);}}
void run(const char* name, bool expected, const char* first, unsigned pause,
         const char* second, unsigned tailPause=0) {
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"bind");
    int length=sizeof address;getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);
    listen(listener,1);
    auto& config=dawn::core::settings::testSettings.client.externalServer;
    strcpy_s(config.host.data(),config.host.size(),"127.0.0.1");config.httpPort=ntohs(address.sin_port);
    bool bodyMatched=false;
    std::thread server([=,&bodyMatched]{SOCKET peer=accept(listener,nullptr,nullptr);
        if(peer!=INVALID_SOCKET){char bytes[4096];std::string incoming;
            for(;;){const int received=recv(peer,bytes,sizeof bytes,0);if(received<=0)break;
                incoming.append(bytes,static_cast<std::size_t>(received));
                const auto marker=incoming.find("\r\n\r\n");
                if(marker!=std::string::npos&&incoming.size()>=marker+8){
                    bodyMatched=incoming.substr(marker+4)==std::string("\x00\x5a\x7f\xff",4);break;}}

            if(first&&*first)send(peer,first,static_cast<int>(std::strlen(first)),0);
            Sleep(pause);
            if(second&&*second)send(peer,second,static_cast<int>(std::strlen(second)),0);
            Sleep(tailPause);closesocket(peer);}closesocket(listener);});
    std::array<std::byte,128> output{};
    dawn::client::network::HttpRequest request{};request.url="/test";request.response=output;
    const std::array<std::byte,4> post{std::byte{0},std::byte{0x5a},std::byte{0x7f},std::byte{0xff}};request.body=post;
    dawn::client::network::HttpResponse response{};
    const auto start=GetTickCount64();const bool result=transport::consume(request,response);
    const auto elapsed=GetTickCount64()-start;
    check(result==expected,name);check(elapsed<3600,"deadline bounded");
    if(expected){check(response.statusCode==200&&response.size==2,"complete response");
        check(output[0]==std::byte{'o'}&&output[1]==std::byte{'k'},"body preserved");}
    else check(response.size==0&&response.statusCode==0,"failure not committed");
    if(pause+tailPause>=3000)check(elapsed>=2800,"deadline not premature");
    server.join();check(bodyMatched,"POST body byte exact");std::printf("%s elapsed=%llu\n",name,elapsed);
}
void write_backpressure() {
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    const int smallBuffer=1024;
    setsockopt(listener,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&smallBuffer),sizeof smallBuffer);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"write bind");
    int size=sizeof address;getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size);listen(listener,1);
    dawn::core::settings::testSettings.client.externalServer.httpPort=ntohs(address.sin_port);
    std::thread server([=]{SOCKET peer=accept(listener,nullptr,nullptr);
        if(peer!=INVALID_SOCKET){Sleep(3400);closesocket(peer);}closesocket(listener);});
    std::vector<std::byte> body(32U*1024U*1024U,std::byte{0x5a});
    std::array<std::byte,16> output{};
    dawn::client::network::HttpRequest request{};request.url="/backpressure";
    request.body=body;request.response=output;
    dawn::client::network::HttpResponse response{};const auto begin=GetTickCount64();
    check(!transport::consume(request,response),"write backpressure timeout");
    const auto elapsed=GetTickCount64()-begin;
    check(elapsed>=2800&&elapsed<3600,"write deadline bounded");
    check(response.size==0&&response.statusCode==0,"write failure uncommitted");
    server.join();std::printf("write backpressure elapsed=%llu\n",elapsed);
}
int main(){WSADATA data{};WSAStartup(MAKEWORD(2,2),&data);
    run("valid",true,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok",0,"");
    run("fragmented valid",true,"HTTP/1.1 200 OK\r\nContent-Len",50,"gth: 2\r\n\r\nok");
    run("malformed",false,"HTTP/1.1 200 OK\r\nContent-Length: nope\r\n\r\n",0,"");
    run("response blackhole",false,"",3200,"");
    run("partial header timeout",false,"HTTP/1.1 200 OK\r\nContent-",3200,"");
    run("shared header/body deadline",false,"HTTP/1.1 200 OK\r\n",2000,"Content-Length: 2\r\n\r\no",1200);
    SOCKET refused=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};
    a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    bind(refused,reinterpret_cast<sockaddr*>(&a),sizeof a);int n=sizeof a;
    getsockname(refused,reinterpret_cast<sockaddr*>(&a),&n);closesocket(refused);
    dawn::core::settings::testSettings.client.externalServer.httpPort=ntohs(a.sin_port);
    dawn::client::network::HttpRequest req{};req.url="/";dawn::client::network::HttpResponse response{};
    const auto begin=GetTickCount64();check(!transport::consume(req,response),"refused connect");
    check(GetTickCount64()-begin<3600,"refusal bounded");
    synBlackhole=true;const auto blocked=GetTickCount64();
    check(!transport::consume(req,response),"SYN blackhole refused at deadline");
    const auto blockedTime=GetTickCount64()-blocked;
    check(blockedTime>=2800&&blockedTime<3600,"SYN blackhole bounded");
    synBlackhole=false;write_backpressure();WSACleanup();
    std::printf("failures=%d\n",failures);return failures?1:0;}

