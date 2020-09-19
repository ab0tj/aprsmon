#ifndef INC_APRSIS_H
#define INC_APRSIS_H

#include <string>
#include <vector>
#include <fap.h>
#include "api.h"

namespace APRS
{
    class ConfigClass
    {
        public:
            std::string myCall;
            std::string aprsHost;
            std::string aprsPort;
            std::string passCode;
    };

    extern ConfigClass* Config;

    class Subscription
    {
        public:
            enum SubscriptionType { Telemetry, Weather };
            Subscription(SubscriptionType t, API::SignalContact c, std::string filterString);

        private:
            SubscriptionType type;
            API::SignalContact contact;
            std::string filter;
            uint dbPriKey;
    };

    class Station
    {
        public:
            Station(std::string callsign);
            void AddSubscription(Subscription s);
            void ProcessPacket(std::string packet);
            std::string call;

        private:
            std::vector<Subscription> subscriptions;
            uint dbPriKey;
    };

    class ISStatus
    {
        public:
            enum ConnectionStatus{ Disconnected, Connecting, Connected };
            ConnectionStatus state;
            std::string filter;
    };

    class ISConnection
    {
        public:
            ISConnection(std::string server, std::string port, std::string call, std::string pass);
            ~ISConnection();
            ISStatus GetStatus();
            std::string SendPacket(std::string packet);
            void RegisterCallback(void(*cbFunc)(std::string));
            std::thread SocketReader();
            void SetFilter(std::string f);

        private:
            ISStatus aprsIsStatus;
            int sockFD;
            std::string aprsHost, aprsPort, aprsCall, aprsPass;
            std::vector<void(*)(std::string)> callbacks;
            void SocketReaderLoop();
            int ReadLine(std::string &line);
            void Connect();
    };

    class Packet
    {
        public:
            Packet(std::string t);
            ~Packet();
            std::string text;
            fap_packet_t* packet;

    };

    class Parser
    {
        public:
            Parser();
            ~Parser();
            void Parse(Packet &p);
            const std::string TypeToString(fap_packet_type_t t);
            const std::string PathToString(char** path, int len);
            const std::string CommentToString(char* comment, int len);
            const std::string WxToString(fap_wx_report_t* wx);
            const std::string TelemetryToString(fap_telemetry_t* t);
    };
}

#endif