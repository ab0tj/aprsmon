#include <iostream>
#include <vector>
#include <algorithm>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include "api.h"
#include "config.h"

namespace API
{
    ConfigClass* Config;

    Listener::Listener(std::string host, uint port)
    {
        apiHost = host;
        apiPort = port;
    }

    Listener::~Listener()
    {
        close(sockFD);
    }

    std::thread Listener::ListenerThread()
    {
        std::thread listener(&Listener::ListenerLoop, this);
        return listener;
    }

    void Listener::ListenerLoop()
    {
        int opt = 1;
        int newSocket, activity, max_sd;
        int valread = 0;
        char buffer[1500];
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        fd_set readfds;
        std::vector<int> clientSockets;

        close(sockFD);
        sockFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockFD < 0)
        {
            std::cerr << "API: Failed to create socket\n";
            throw std::exception();
        }

        if (setsockopt(sockFD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
            std::cerr << "API: Failed to set socket options\n";
            throw std::exception();
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(apiHost.c_str());
        address.sin_port = htons(apiPort);

        if (bind(sockFD, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            std::cerr << "API: Failed to bind socket " << errno << '\n';
            throw std::exception();
        }

        if (listen(sockFD, 10) < 0)
        {
            std::cerr << "API: Failed listen\n";
            throw std::exception();
        }

        for (;;)
        {
            FD_ZERO(&readfds);
            FD_SET(sockFD, &readfds);
            max_sd = sockFD;

            for (int sd : clientSockets)
            {
                if (sd > 0) FD_SET(sd, &readfds);
                if (sd > max_sd) max_sd = sd;
            }

            activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
            if ((activity < 0) && (errno != EINTR))
            {
                std::cerr << "API: Select error\n";
            }

            if (FD_ISSET(sockFD, &readfds))
            {
                /* Client connected */
                if ((newSocket = accept(sockFD, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0)
                {
                    std::cerr << "API: Accept error\n";
                    continue;
                }

                /*int yes = 1;
                struct timeval tv;
                tv.tv_sec = 30;
                tv.tv_usec = 0;
                setsockopt(newSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
                setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(int));*/

                if (BaseConfig::Config->debug)
                {
                    std::cout << "API: Accepted connection, socket FD " << newSocket << " address ";
                    std::cout << inet_ntoa(address.sin_addr) << ':' << ntohs(address.sin_port) << '\n';
                }

                clientSockets.push_back(newSocket);
            }
            
            for (int sd : clientSockets)
            {
                if (FD_ISSET(sd, &readfds))
                {
                    valread = read(sd, buffer, sizeof(buffer));
                    if (valread == 0)
                    {
                        /* Client disconnected */
                        if (BaseConfig::Config->debug)
                        {
                            getpeername(sd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
                            std::cout << "API: Client disconnected, socket FD " << sd << " address ";
                            std::cout << inet_ntoa(address.sin_addr) << ':' << ntohs(address.sin_port) << '\n';
                        }

                        close(sd);
                        clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), sd), clientSockets.end()); 
                    }

                    else
                    {
                        /* Client sent data */
                        buffer[valread] = '\0';
                        if (BaseConfig::Config->debug) std::cout << "API: Client sent " << buffer;
                        send(sd, buffer, valread, 0);
                    }
                }
            }
        }
    }
}