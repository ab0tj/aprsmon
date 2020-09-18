#ifndef INC_API_H
#define INC_API_H

#include <string>
#include <thread>

namespace API
{
    class SignalContact
    {
        public:
            enum ContactType { User, Group };
            SignalContact(ContactType t, std::string callSgn, std::string contactID);
            std::string Notify(std::string message);

        private:
            std::string callsign;
            std::string id;
            ContactType type;
    };

    class SignalMessage
    {
        public:
            SignalMessage(SignalContact c, std::string msg);
            SignalMessage(SignalContact c, uint64_t tStamp, std::string msg);
            std::string MarkRead();
            std::string Send(std::string message);

        private:
            SignalContact contact;
            uint64_t timeStamp;
            std::string message;
    }

    class Call
    {
        public:
            enum CallType { aprStat, aprsMessage, aprsSubscribe };
            Call(CallType t, SignalMessage msg);

        private:
            CallType type;
            SignalMessage message;
            std::thread callThread;
            int sockFD;
    }

    SignalContact getContactByName(std::string n);
}

#endif