#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include "api.h"
#include "config.h"
#include "packet.h"
#include "database.h"

namespace API
{
    const std::unordered_map<std::string, CallType> CallTypeMap = {
        {"invalid",Invalid},
        {"aprstats",aprStats},
        {"aprsMessage",aprsMessage},
        {"aprsSubscribe",aprsSubscribe},
        {"aprsUnsubscribe",aprsUnsubscribe},
        {"modUser",modUser}
    };

    std::unordered_map<std::string, SignalContact> SignalContacts;

    ConfigClass* Config;
    void CallHandler(std::string text, int socket);
    void AprStats(int socket, json &j);
    void AprsMessage(int socket, json &j);
    void AprsSubscribe(int socket, json &j);
    void AprsUnsubscribe(int socket, json &j);
    void ModUser(int socket, json &j);
    bool ChatBotApiCall(json& j);

    Listener::Listener()
    {
        apiHost = Config->listenIP;
        apiPort = Config->listenPort;
    }

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

        if (sockFD) close(sockFD);
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

        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(sockFD, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sockFD, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(int));

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

                        try
                        {
                            std::string s(buffer);
                            std::thread t(CallHandler, s, sd);
                            t.detach();
                        }
                        catch(const std::exception& e)
                        {
                            if (BaseConfig::Config->debug) std::cerr << "API call error: " << e.what() << '\n';
                        }
                    }
                }
            }
        }
    }

    void SendApiReply(int socket, json &j, Error e = Error::none)
    {
        j["errorCode"] = e;
        std::string buffer(j.dump() + '\n');
        send(socket, buffer.c_str(), buffer.length(), 0);
    }

    void CallHandler(std::string text, int socket)
    {
        json j, reply;

        try
        {
            j = json::parse(text);
        }
        catch(const std::exception& e)
        {
            SendApiReply(socket, reply, Error::json_error);
            return;
        }

        if (!j.contains("type"))
        {
            SendApiReply(socket, reply, Error::param_missing);
            return;
        }

        else
        {
            CallType type;
            auto it = CallTypeMap.find(j["type"]);
            if (it != CallTypeMap.end())
            {
                type = it->second;
            }
            else
            {
                type = Invalid;
            }
            

            reply["type"] = j["type"];

            switch(type)
            {
                case aprStats:
                {
                    AprStats(socket, j);
                    return;
                }

                case aprsMessage:
                    AprsMessage(socket, j);
                    return;

                case aprsSubscribe:
                    AprsSubscribe(socket, j);
                    return;

                case aprsUnsubscribe:
                    AprsUnsubscribe(socket, j);
                    return;

                case modUser:
                    ModUser(socket, j);
                    break;

                case Invalid:
                    SendApiReply(socket, reply, Error::inv_api_call);
                    break;
            } 
        }
    }

    void AprStats(int socket, json &j)
    {
        json reply;
        reply["type"] = j["type"];

        if (!j.contains("contactID") || !j.contains("groupContact") || j.at("contactID").type() != json::value_t::string
            || j.at("groupContact").type() != json::value_t::boolean)
        {
            SendApiReply(socket, reply, Error::param_missing);
            return;
        }

        SignalContact c(j);
        reply["ID"] = rand();
        SendApiReply(socket, reply);
        if (BaseConfig::Config->debug) std::cout << "API: " << c.id << " requested AprStats " << c.callsign << '\n';

        c.Notify("This function is under construction.");
    }

    void AprsMessage(int socket, json &j)
    {
        json reply;
        reply["type"] = j["type"];

        if (!j.contains("contactID") || !j.contains("groupContact") || !j.contains("station") || !j.contains("message") ||
            !j.contains("timestamp") || !j.contains("readRecipient"))
        {
            SendApiReply(socket, reply, Error::param_missing);
            return;
        }
        
        SignalMessage m = SignalMessage(j);
        reply["ID"] = rand();
        SendApiReply(socket, reply);

        std::string text = j.at("message");

        if (text.length() > 67)
        {
            m.contact.Notify("Sorry, APRS limits messages to 67 characters.");
            return;
        }
        if (m.contact.callsign.length() > 9)
        {
            m.contact.Notify("Destination callsign must be 9 characters or less.");
            return;
        }

        if (m.contact.callsign.compare("") == 0)
        {
            APRS::Conversation* conv = APRS::Conversation::Get(APRS::Conduit_Signal, m.contact.id);
            if (conv != NULL)
            {
                m.contact.callsign = conv->aprsStn;
            }
            else
            {
                m.contact.Notify("Sorry, you are not in a conversation. Try !@<callsign>");
                return;
            }
        }
        else
        {
            APRS::Conversation::Add(APRS::Conduit_Signal, m.contact.callsign, m.contact);
        }

        APRS::Message msg(text, m.contact);
        msg.Send();

        if (BaseConfig::Config->debug) std::cout << "API: " << m.contact.id << " sent APRS message to " << m.contact.callsign << '\n';
    }

    void AprsSubscribe(int socket, json &j)
    {
        json reply;
        reply["type"] = j["type"];

        if (!j.contains("contactID") || !j.contains("groupContact") || !j.contains("filter"))
        {
            SendApiReply(socket, reply, Error::param_missing);
            return;
        }

        SignalContact c = SignalContact(j);
        reply["ID"] = rand();
        SendApiReply(socket, reply);
        if (BaseConfig::Config->debug) std::cout << "API: " << c.id << " subscribed with filter " << j["filter"] << '\n';
    }

    void AprsUnsubscribe(int socket, json &j)
    {
        json reply;
        reply["type"] = j["type"];

        if (!j.contains("contactID") || !j.contains("groupContact") || !j.contains("id"))
        {
            SendApiReply(socket, reply, Error::param_missing);
            return;
        }

        SignalContact c = SignalContact(j);
        reply["id"] = rand();
        SendApiReply(socket, reply);
        if (BaseConfig::Config->debug) std::cout << "API: " << c.id << " unsubscribed from " << j["id"] << '\n';
    }

    void ModUser(int socket, json &j)
    {
        json reply;
        reply["type"] = j["type"];

        if (!j.contains("contactID") || !j.contains("groupContact") || !j.contains("action") || !j.contains("station"))
        {
            SendApiReply(socket, reply, Error::param_missing);
            return;
        }
        if ((std::string)j["action"] != "add" && (std::string)j["action"] != "del")
        {
            SendApiReply(socket, reply, Error::param_invalid);
            return;
        }

        reply["id"] = rand();
        SendApiReply(socket, reply);

        SignalContact c = SignalContact(j);
        std::string action = j.at("action");

        if (action.compare("add") == 0)
        {
            APRS::Station s(j.at("station"));
            s.monitorFlags[APRS::SubscriptionType::Messages] = 1;
            Packet::AddMonitoredStation(s);
        }
        else Packet::DelMonitoredStation(j.at("station"));

        if (BaseConfig::Config->debug) std::cout << "API: modUser " << j["action"] << ' ' << c.id << '\n';
    }

    SignalContact::SignalContact(json &j)
    {
        if (j.contains("station") && j.at("station").type() != json::value_t::null)
        {
            callsign = j["station"];
        }
        else callsign = "";

        id = j["contactID"];
        group = j["groupContact"];
        dbPriKey = GetPriKey();
    }

    SignalContact::SignalContact(std::string contactID, bool groupContact, std::string call)
    {
        id = contactID;
        group  = groupContact;
        callsign = call;
        dbPriKey = GetPriKey();
    }

    uint SignalContact::GetPriKey()
    {
        Database::Query q("INSERT INTO contacts (SignalID, IsGroup) VALUES ('");
        q.text += id + "', " + std::to_string(group) + ") ";
        q.text += "ON DUPLICATE KEY UPDATE SignalID = VALUES(SignalID), IsGroup = VALUES(IsGroup)";
        Database::Connection->Execute(q);

        q.text = "SELECT PriKey FROM contacts WHERE SignalID = '" + id + '\'';
        Database::Connection->Execute(q);
        return std::stoi(mysql_fetch_row(q.result)[0]);
    }

    bool SignalContact::Notify(std::string message)
    {
        json j;
        j["type"] = "notify";
        j["contactID"] = id;
        j["groupContact"] = group;
        j["message"] = message;
        ChatBotApiCall(j);

        return (j.contains("sent") && j.at("sent") == true);
    }

    SignalMessage::SignalMessage(json &j)
    {
        contact = SignalContact(j);
        timeStamp = j["timestamp"];
        message = j["message"];
    }

    SignalMessage::SignalMessage(SignalContact& c, uint64_t tStamp, std::string msg)
    {
        contact = c;
        timeStamp = tStamp;
        message = msg;
    }

    void SignalMessage::MarkRead()
    {
        json j;
        j["type"] = "markRead";
        j["timestamp"] = timeStamp;
        j["readRecipient"] = contact.id;
        ChatBotApiCall(j);
    }

    bool ChatBotApiCall(json& j)
    {
        int sockFD;
        ssize_t val;
        struct addrinfo hints = {0}, *addrs;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        const int status = getaddrinfo(Config->signalBotHost.c_str(), std::to_string(Config->signalBotPort).c_str(), &hints, &addrs);
        if (status != 0)
        {
            std::cerr << "API: ChatBot hostname resolution failure: " << gai_strerror(status) << " (" << Config->signalBotHost << ")\n";
            return false;
        }

        int err = 0;
        for (struct addrinfo *addr = addrs; addr != nullptr; addr = addr->ai_next)
        {
            sockFD = socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol);
            if (sockFD < 0)
            {
                err = errno;
                continue;
            }

            if (connect(sockFD, addr->ai_addr, addr->ai_addrlen) == 0) break;

            err = errno;
            sockFD = -1;
            close(sockFD);

            freeaddrinfo(addrs);
        }

        if (sockFD < 0)
        {
            std::cerr << "API: Connect to ChatBot failed: " << strerror(err) << " (" << Config->signalBotHost << ")\n";
            return false;
        }

        int yes = 1;
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(sockFD, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sockFD, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(int));

        if (BaseConfig::Config->debug) std::cout << "API: Connected to GeekChatBot\n";

        char buffer[1024];
        std::string text = j.dump();
        val = write(sockFD, text.c_str(), text.size());
        if (val == (ssize_t)text.size())
        {
            if (BaseConfig::Config->debug) std::cout << "API: Sent to GCB: " << text << '\n';
        }
        else
        {
            std::cerr << "API: Failed to write to ChatBot\n";
            return false;
        }

        val = read(sockFD, buffer, sizeof(buffer));
        if (val > 0)
        {
            buffer[val] = '\0';
            if (BaseConfig::Config->debug) std::cout << "API: GCB replied: " << buffer << '\n';
        }
        else
        {
            if (BaseConfig::Config->verbose) std::cout << "API: No reply from ChatBot\n";
            return false;
        }

        close(sockFD);
        
        j = json::parse(buffer);

        return !j.contains("error");
    }

    SignalContact ContactLookup(std::string destCall, std::string srcCall)
    {
        json j;
        SignalContact c;

        if (destCall.find_first_of('/') != std::string::npos)
        {
            j["type"] = "groupAliasLookup";
            j["alias"] = destCall;
            ChatBotApiCall(j);

            if (j.contains("groupID") && j.at("groupID").type() != json::value_t::null)
            {
                c.id = j.at("groupID");
                c.group = true;
                return c;
            }
        }

        j["type"] = "callLookup";
        j["callsign"] = destCall;
        ChatBotApiCall(j);
        if (j.contains("contactID") && j.at("contactID").type() != json::value_t::null)
        {
            c.id = j.at("contactID");
            c.group = false;
        }

        return c;
    }
}