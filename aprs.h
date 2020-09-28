#ifndef INC_APRSIS_H
#define INC_APRSIS_H

#include <string>
#include <vector>
#include <bitset>
#include <fap.h>
#include "api.h"

namespace APRS
{
    class ConfigClass
    {
        public:
            std::string myCall;
            std::string idCall;
            std::string aprsHost;
            std::string aprsPort;
            std::string passCode;
    };

    extern ConfigClass* Config;

    enum SubscriptionType
    {
        Messages,
        Telemetry,
        Weather,
        Activity,
        Watchdog,
        Stats,
        Position,
        Digi,
        Igate,
        SUBSCRIPTION_TYPE_ITEMS
    };

    typedef std::bitset<SUBSCRIPTION_TYPE_ITEMS> subscription_bits;

    class Subscription
    {
        public:
            Subscription(SubscriptionType t, API::SignalContact c, std::string filterString, uint priKey);
            SubscriptionType type;
            API::SignalContact contact;
            std::string filter;
            uint dbPriKey;
    };

    typedef struct
    {
        float lat;
        float lon;
    } Position_t;

    class Station
    {
        public:
            Station(std::string callsign = "");
            void ProcessPacket(std::string packet);
            std::string call;
            subscription_bits monitorFlags;
            subscription_bits stateFlags;
            uint dbPriKey;
            time_t lastHeard;
            time_t lastDigi;
            time_t lastIgate;
            Position_t pos;
            std::vector<Subscription> subscriptions;
            API::SignalContact contact;
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
            ISConnection();
            ISConnection(std::string server, std::string port, std::string call, std::string pass);
            ~ISConnection();
            ISStatus GetStatus();
            int Send(std::string& text);
            void RegisterCallback(void(*cbFunc)(std::string&));
            std::thread SocketReader();
            void SetFilter(std::string f);

        private:
            ISStatus aprsIsStatus;
            int sockFD;
            std::string aprsHost, aprsPort, aprsCall, aprsPass;
            std::vector<void(*)(std::string&)> callbacks;
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

    class Message
    {
        public:
            Message(std::string& text, API::SignalContact& c);
            Message(std::string& text, API::SignalContact& c, uint msgId);
            int Send();

        private:
            std::string msg;
            API::SignalContact contact;
            uint id;
    };

    enum MsgConduitType { Conduit_Signal, Conduit_APRS };

    class Conversation
    {
        public:
            static Conversation* Get(MsgConduitType con, std::string id);
            static void Add(MsgConduitType con, std::string call, API::SignalContact& c);
            static void Remove(MsgConduitType con, std::string id);
            std::string aprsStn;
            API::SignalContact signalContact;

        private:
            Conversation(std::string call, API::SignalContact& c);
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

    void DumpPacket(fap_packet_t* pkt);
    void AckMsg(fap_packet_t* msg, bool nack = false);
    void SendMessage(std::string& call, std::string& msg, uint id);

    extern Parser* aprsParser;
    extern ISConnection* Connection;
}

#endif