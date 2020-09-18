#ifndef INC_APRSIS_H
#define INC_APRSIS_H

#include <string>
#include <vector>
#include "api.h"

namespace APRS
{
    class ConfigClass
    {
        public:
            std::string myCall;
            std::string aprsHost;
            uint aprsPort;
            int passCode;
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

    class AprsISStatus
    {
        public:
            bool connected;
            time_t lastHeard;
            std::string filter;
    };

    class AprsISConnection
    {
        public:
            AprsISConnection(std::string server, std::string call, std::string passcode);
            ~AprsISConnection();
            AprsISStatus GetStatus();
            std::string SendPacket(std::string packet);
            void AddPacketCallback(void(*cbFunc)(std::string));

        private:
            AprsISStatus status;
            int sockFD;
            std::vector<void *(*)(std::string)> callbacks;
    };
}

#endif