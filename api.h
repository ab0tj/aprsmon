#ifndef INC_API_H
#define INC_API_H

#include <string>
#include <thread>
#include <unordered_map>
#include "json.hpp"

using json = nlohmann::json;

namespace API
{
    class SignalContact
    {
        public:
            SignalContact(std::string contactID, bool groupContact, std::string call = "");
            SignalContact(json &j);
            SignalContact() = default;
            std::string callsign;
            std::string id;
            bool group;
            uint dbPriKey;
            bool Notify(std::string message);

        private:
            uint GetPriKey();
    };
    extern std::unordered_map<std::string, SignalContact> SignalContacts;

    class ConfigClass
    {
        public:
            std::string listenIP;
            uint listenPort;
            std::string signalBotHost;
            uint signalBotPort;
            SignalContact admin;
            bool adminNotofications;
    };
    extern ConfigClass* Config;

    class SignalMessage
    {
        public:
            SignalMessage(SignalContact& c, uint64_t tStamp, std::string msg);
            SignalMessage(json& j);
            void MarkRead();
            SignalContact contact;
            uint64_t timeStamp;
            std::string message;
    };

    class Listener
    {
        public:
            Listener();
            Listener(std::string host, uint port);
            ~Listener();
            std::thread ListenerThread();

        private:
            int sockFD;
            std::string apiHost;
            uint apiPort;
            void ListenerLoop();
    };

    SignalContact ContactLookup(std::string destCall, std::string srcCall = "");
    enum CallType { Invalid, aprStats, aprsMessage, aprsSubscribe, aprsUnsubscribe, modUser };
    enum Error { none, json_error, inv_api_call, param_missing, param_invalid };
}

#endif