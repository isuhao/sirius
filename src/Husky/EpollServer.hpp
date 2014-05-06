#ifndef HUSKY_EPOLLSERVER_H 
#define HUSKY_EPOLLSERVER_H 

#include <stdio.h>
#include <string.h>

#include <cassert>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <vector>
#include <sys/epoll.h>
#include <fcntl.h>
#include "HttpReqInfo.hpp"



namespace Husky
{
    using namespace Limonp;

    const char* const HTTP_FORMAT = "HTTP/1.1 200 OK\r\nConnection: close\r\nServer: HuskyServer/1.0.0\r\nContent-Type: text/json; charset=%s\r\nContent-Length: %d\r\n\r\n%s";
    const char* const CHARSET_UTF8 = "UTF-8";
    const char* const CLIENT_IP_K = "CLIENT_IP"; 

    const struct linger LNG = {1, 1};
    const struct timeval SOCKET_TIMEOUT = {5, 0};

    class IRequestHandler
    {
        public:
            virtual ~IRequestHandler(){};
        public:
            virtual bool do_GET(const HttpReqInfo& httpReq, string& res) const = 0;
            virtual bool do_POST(const HttpReqInfo& httpReq, string& res) const = 0;
    };

    class EpollServer
    {
        private:
            static const size_t LISTEN_QUEUE_LEN = 1024;
            static const size_t RECV_BUFFER_SIZE = 1024*4;
            static const int MAXEPOLLSIZE = 512;

        private:
            const IRequestHandler* _reqHandler;
            int _host_socket;
            int _epoll_fd;
            bool _isShutDown;
            int _epollSize;
            unordered_map<int, string> _sockIpMap;
        private:
            bool _isInited;
            bool _getInitFlag() const {return _isInited;}
            bool _setInitFlag(bool flag) {return _isInited = flag;} 
        public:
            explicit EpollServer(uint port, const IRequestHandler* pHandler): _reqHandler(pHandler), _host_socket(-1), _isShutDown(false), _epollSize(0)
        {
            assert(_reqHandler);
            _setInitFlag(_init_epoll(port));
        };
            ~EpollServer(){};// unfinished;
        public:
            operator bool() const
            {
                return _getInitFlag();
            }
        public:
            bool start()
            {
                //int clientSock;
                sockaddr_in clientaddr;
                socklen_t nSize = sizeof(clientaddr);
                struct epoll_event events[MAXEPOLLSIZE];
                int nfds, clientSock;

                while(!_isShutDown)
                {
                    if(-1 == (nfds = epoll_wait(_epoll_fd, events, _epollSize, -1)))
                    {
                        LogFatal(strerror(errno));
                        return false;
                    }

                    //LogDebug("epoll_wait return event sum[%d]", nfds);

                    for(int i = 0; i < nfds; i++)
                    {
                        if(events[i].data.fd == _host_socket) /*new connect coming.*/
                        {
                            if(-1 == (clientSock = accept(_host_socket, (struct sockaddr*) &clientaddr, &nSize)))
                            {
                                LogError(strerror(errno));
                                continue;
                            }
                            if(!_epoll_add(clientSock, EPOLLIN | EPOLLET))
                            {
                                LogError("_epoll_add(%d, EPOLLIN | EPOLLET)", clientSock);
                                _closesocket(clientSock);
                                continue;
                            }

                            //LogInfo("connecting from: %d:%d， client socket: %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), clientSock);

                            /* inet_ntoa is not thread safety at some version  */
                            //_sockIpMap[clientSock] = inet_ntoa(clientaddr.sin_addr);

                        }
                        else /*client socket data to be received*/
                        {
                            _response(events[i].data.fd);

                            /*close socket will case it to be removed from epoll automatically*/
                            _closesocket(events[i].data.fd);
                        }
                    }

                }
                return true;
            }
            void stop()
            {
                _isShutDown = true;
                if(-1 == close(_host_socket))
                {
                    LogError(strerror(errno));
                    return;
                }
                int sockfd;
                struct sockaddr_in dest;
                if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                {
                    LogError(strerror(errno));
                    return;
                }
                bzero(&dest, sizeof(dest));
                dest.sin_family = AF_INET;
                dest.sin_port = htons(_host_socket);
                if(0 == inet_aton("127.0.0.1", (struct in_addr *) &dest.sin_addr.s_addr))
                {
                    LogError(strerror(errno));
                    return;
                }
                if(connect(sockfd, (struct sockaddr *) &dest, sizeof(dest)) < 0)
                {
                    LogError(strerror(errno));
                }
                _closesocket(sockfd);
            }
        private:
            bool _epoll_add(int sockfd, uint32_t events)
            {
                if (!_setNonBLock(sockfd)) 
                {
                    LogError(strerror(errno));
                    return false;
                }
                struct epoll_event ev;
                ev.data.fd = sockfd;
                ev.events = events;
                if(epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
                {
                    LogError("insert socket '%d' into epoll failed: %s", sockfd, strerror(errno));
                    return false;
                }
                _epollSize ++;
                return true;
            }
            bool _response(int sockfd) const
            {
                if(-1 == setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (const char*)&LNG, sizeof(LNG)))
                {
                    LogError(strerror(errno));
                    return false;
                }
                if(-1 == setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&SOCKET_TIMEOUT, sizeof(SOCKET_TIMEOUT)))
                {
                    LogError(strerror(errno));
                    return false;
                }
                if(-1 == setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&SOCKET_TIMEOUT, sizeof(SOCKET_TIMEOUT)))
                {
                    LogError(strerror(errno));
                    return false;
                }

                string strRec, strSnd, strRetByHandler;
                char recvBuf[RECV_BUFFER_SIZE];
                int nRetCode = -1;
                while(true)
                {
                    memset(recvBuf, 0, sizeof(recvBuf));
                    nRetCode = recv(sockfd, recvBuf, sizeof(recvBuf) - 1, 0);
                    if(-1 == nRetCode)
                    {
                        LogDebug(strerror(errno));
                        return false;
                    }
                    if(0 == nRetCode)
                    {
                        LogDebug("client socket orderly shut down");
                        return false;
                    }
                    strRec += recvBuf;
                    if(nRetCode != sizeof(recvBuf) - 1)
                    {
                        break;
                    }
                }

                HttpReqInfo httpReq(strRec);
                if("GET" == httpReq.getMethod() && !_reqHandler->do_GET(httpReq, strRetByHandler))
                {
                    LogError("do_GET failed.");
                    return false;
                }
                if("POST" == httpReq.getMethod() && !_reqHandler->do_POST(httpReq, strRetByHandler))
                {
                    LogError("do_POST failed.");
                    return false;
                }
                string_format(strSnd, HTTP_FORMAT, CHARSET_UTF8, strRetByHandler.length(), strRetByHandler.c_str());

                if(-1 == send(sockfd, strSnd.c_str(), strSnd.length(), 0))
                {
                    LogError(strerror(errno));
                    return false;
                }
                LogInfo("{response:%s, epollsize:%d}", strRetByHandler.c_str(), _epollSize);
                return true;
            }
            bool _init_epoll(uint port)
            { 
                _host_socket = socket(AF_INET, SOCK_STREAM, 0);
                if(-1 == _host_socket)
                {
                    LogError(strerror(errno));
                    return false;
                }

                int nRet = 1;
                if(-1 == setsockopt(_host_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&nRet, sizeof(nRet)))
                {
                    LogError(strerror(errno));
                    return false;
                }

                struct sockaddr_in addrSock;
                addrSock.sin_family = AF_INET;
                addrSock.sin_port = htons(port);
                addrSock.sin_addr.s_addr = htonl(INADDR_ANY);
                if(-1 == ::bind(_host_socket, (sockaddr*)&addrSock, sizeof(sockaddr)))
                {
                    LogError(strerror(errno));
                    _closesocket(_host_socket);
                    return false;
                }
                if(-1 == listen(_host_socket, LISTEN_QUEUE_LEN))
                {
                    LogError(strerror(errno));
                    return false;
                }

                if(-1 == (_epoll_fd = epoll_create(MAXEPOLLSIZE)))
                {
                    LogError(strerror(errno));
                    return false;
                }
                if(!_epoll_add(_host_socket, EPOLLIN))
                {
                    LogError("_epoll_add(%d, EPOLLIN) failed.", _host_socket);
                    return false;
                }
                LogInfo("create socket listening port[%u], epoll{size:%d} init ok", port, _epollSize);
                return true;
            }
            void _closesocket(int sockfd)
            {
                if(-1 == close(sockfd))
                {
                    LogError(strerror(errno));
                    return;
                }
                _epollSize--;
            }
            static bool _setNonBLock(int sockfd)
            {
                return -1 != fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK);
            }
    };
}
#endif
