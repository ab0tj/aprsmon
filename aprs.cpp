#include <iostream>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <chrono>
#include <netinet/tcp.h>
#include "aprs.h"
#include "config.h"
#include "database.h"

namespace APRS
{
    ConfigClass* Config;
    Parser* aprsParser;
    ISConnection* Connection;
    uint nextMsgId = 0;
    std::unordered_map<std::string, std::string> signalConvos;
    std::unordered_map<std::string, Conversation> aprsConvos;

    ISConnection::ISConnection()
    {
        aprsHost = Config->aprsHost;
        aprsPort = Config->aprsPort;
        aprsCall = Config->myCall;
        aprsPass = Config->passCode;
        aprsIsStatus.state = ISStatus::Disconnected;
    }

    ISConnection::ISConnection(std::string server, std::string port, std::string call, std::string pass)
    {
        aprsHost = server;
        aprsPort = port;
        aprsCall = call;
        aprsPass = pass;
        aprsIsStatus.state = ISStatus::Disconnected;
    }

    ISConnection::~ISConnection()
    {
        if (sockFD) close(sockFD);
    }

    void ISConnection::Connect()
    {
        std::string buffer;
        aprsIsStatus.state = ISStatus::Connecting;

        if (sockFD) close(sockFD);

        Database::Query q("SELECT MAX(IFNULL(ID, 0)) FROM messages");
        Database::Connection->Execute(q);
        nextMsgId = std::stoi(mysql_fetch_row(q.result)[0]) + 1;

        struct addrinfo hints = {0}, *addrs;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        const int status = getaddrinfo(aprsHost.c_str(), aprsPort.c_str(), &hints, &addrs);
        if (status != 0)
        {
            std::cerr << "APRS-IS: Hostname resolution failure: " << gai_strerror(status) << " (" << aprsHost << ")\n";
            aprsIsStatus.state = ISStatus::Disconnected;
            return;
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
        }

        freeaddrinfo(addrs);

        if (sockFD < 0)
        {
            std::cerr << "APRS-IS: Connect failed: " << strerror(err) << " (" << aprsHost << ")\n";
            aprsIsStatus.state = ISStatus::Disconnected;
            return;
        }

        int yes = 1;
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(sockFD, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sockFD, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(int));

        int readBytes;

        readBytes = ReadLine(buffer);

        if (readBytes < 0)
        {
            std::cerr << "APRS-IS: Connect failed: " << strerror(err) << " (" << aprsHost << ")\n";
            aprsIsStatus.state = ISStatus::Disconnected;
            return;
        }

        if (BaseConfig::Config->verbose) std::cout << "APRS-IS Connected! Server says: " << buffer.substr(2, std::string::npos);

        std::string login = "user " + aprsCall + " pass " + aprsPass + " vers " + BaseConfig::versString;
        if (aprsIsStatus.filter.length() > 0) login += " filter " + aprsIsStatus.filter;
        login += "\r\n";
        send(sockFD, login.c_str(), login.length(), 0);

        readBytes = ReadLine(buffer);

        if (readBytes < 0)
        {
            std::cerr << "APRS-IS: Login failed: " << strerror(err) << " (" << aprsHost << ")\n";
            aprsIsStatus.state = ISStatus::Disconnected;
            return;
        }

        if (BaseConfig::Config->verbose) std::cout << "APRS-IS login result: " << buffer.substr(2, std::string::npos);

        aprsIsStatus.state = ISStatus::Connected;
    }

    void ISConnection::RegisterCallback(void(*cbFunc)(std::string&))
    {
        callbacks.push_back(cbFunc);
    }

    std::thread ISConnection::SocketReader()
    {
        std::thread listener(&ISConnection::SocketReaderLoop, this);
        return listener;
    }

    void ISConnection::SocketReaderLoop()
    {
        std::string line;
        int bytes = 0;

        if (aprsIsStatus.state == ISStatus::Disconnected) Connect();

        for (;;)
        {
            if ((bytes = ReadLine(line)) < 0)
            {
                aprsIsStatus.state = ISStatus::Disconnected;
                if (BaseConfig::Config->verbose) std::cout << "APRS-IS disconnected\n";
                
                for (;;)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    if (BaseConfig::Config->verbose) std::cout << "APRS-IS: Trying to reconnect...\n";
                    Connect();
                    if (aprsIsStatus.state == ISStatus::Connected) break;
                }
            }

            else
            {
                if (line[0] != '#')
                {
                    for (void(*cb)(std::string&) : callbacks) cb(line);
                }
            }
        }
    }

    int ISConnection::ReadLine(std::string &line)
    {
        int readBytes = 0;
        static char buffer[1024] = {0};
        uint bufferPos = 0;

        for (;;)
        {
            readBytes = read(sockFD, buffer + bufferPos, 1);

            if (readBytes < 0) return readBytes;

            if (buffer[bufferPos++] == '\n')
            {
                buffer[bufferPos] = 0;
                line = std::string(buffer);
                return bufferPos;
            }
        }
    }

    void ISConnection::SetFilter(std::string f)
    {
        aprsIsStatus.filter = f;
        f.push_back('\n');
        f.insert(0, "#filter ");
        Send(f);
    }

    int ISConnection::Send(std::string& text)
    {
        if (BaseConfig::Config->debug) std::cout << "APRS-IS: Sending: " << text;
        if (Connection->aprsIsStatus.state != ISStatus::Connected) return -1;
        int bytes = send(sockFD, text.c_str(), text.size(), 0);
        return bytes;
    }

    Parser::Parser()
    {
        fap_init();
    }

    Parser::~Parser()
    {
        fap_cleanup();
    }

    void Parser::Parse(Packet &p)
    {
        p.packet = fap_parseaprs(p.text.c_str(), p.text.length(), 0);
    }

    const std::string Parser::TypeToString(fap_packet_type_t t)
    {
        switch (t)
        {
            case fapLOCATION:
                return "Location";
            case fapOBJECT:
                return "Object";
            case fapITEM:
                return "Item";
            case fapMICE:
                return "MIC-E";
            case fapNMEA:
                return "NMEA";
            case fapWX:
                return "Weather";
            case fapMESSAGE:
                return "Message";
            case fapCAPABILITIES:
                return "Capabilities";
            case fapSTATUS:
                return "Status";
            case fapTELEMETRY:
                return "Telemetry";
            case fapTELEMETRY_MESSAGE:
                return "Telemetry Message";
            case fapDX_SPOT:
                return "DX Spot";
            case fapEXPERIMENTAL:
                return "Experimental";

        }
        return "Unknown";
    }

    const std::string Parser::PathToString(char** path, int len)
    {
        std::string p;

        for (int i = 0; i < len;)
        {
            p += (char*)path[i];
            if (++i != len) p += ',';
        }

        return p;
    }

    const std::string Parser::CommentToString(char* comment, int len)
    {
        char temp[len+1];
        memcpy(&temp, comment, len);
        temp[len] = '\0';
        if (temp[len - 2] == '\r') temp[len - 2] = '\0';
        if (temp[len - 1] == '\n') temp[len - 1] = '\0';
        return std::string(temp);
    }

    const std::string Parser::WxToString(fap_wx_report_t* wx)
    {
        std::stringstream s;

        if (wx->wind_gust) s << "Wind Gust:" << (double)*wx->wind_gust << "m/s ";
        if (wx->wind_dir) s << "Wind Dir:" << (uint)*wx->wind_dir << "deg ";
        if (wx->wind_speed) s << "Wind Spd:" << (double)*wx->wind_speed << "m/s ";
        if (wx->temp) s << "Temp:" << (double)*wx->temp << "C ";
        if (wx->temp_in) s << "Indoor Temp:" << (double)*wx->temp_in << "C ";
        if (wx->rain_1h) s << "Rain 1h:" << (double)*wx->rain_1h << "mm ";
        if (wx->rain_24h) s << "Rain 24h:" << (double)*wx->rain_24h << "mm ";
        if (wx->rain_midnight) s << "Rain Since Midnight:" << (double)*wx->rain_midnight << "mm ";
        if (wx->humidity) s << "Humidity:" << (uint)*wx->humidity << "% ";
        if (wx->humidity_in) s << "Indoor Humidity:" << (uint)*wx->humidity_in << "% ";
        if (wx->pressure) s << "Pressure:" << (double)*wx->pressure << "mbar ";
        if (wx->luminosity) s << "Luminosity:" << (uint)*wx->luminosity << "w/m2 ";
        if (wx->snow_24h) s << "Snow 24h:" << (double)*wx->snow_24h << "mm ";
        if (wx->soft) s << "Software:" << wx->soft << ' ';

        std::string temp = s.str();
        return temp.substr(0, temp.length() - 1);
    }

    const std::string Parser::TelemetryToString(fap_telemetry_t* t)
    {
        std::stringstream s;

        if (t->seq) s << "Seq:" << (uint)*t->seq << ' ';
        if (t->val1) s << "Val1:" << (double)*t->val1 << ' ';
        if (t->val2) s << "Val2:" << (double)*t->val2 << ' ';
        if (t->val3) s << "Val3:" << (double)*t->val3 << ' ';
        if (t->val4) s << "Val4:" << (double)*t->val4 << ' ';
        if (t->val5) s << "Val5:" << (double)*t->val5 << ' ';
        if (t->bits) s << "Bits:" << t->bits[0] << t->bits[1] << t->bits[2] << t->bits[3] << t->bits[4];
        if (t->bits) s << t->bits[5] << t->bits[6] << t->bits[7];

        return s.str();
    }

    Packet::Packet(std::string t)
    {
        text = t;
    }

    Packet::~Packet()
    {
        fap_free(packet);
    }

    Station::Station(std::string callsign)
    {
        call = callsign;
        monitorFlags = 0;
        stateFlags = 0;
        dbPriKey = 0;
        lastDigi = lastHeard = lastIgate = 0;
        pos.lat = NAN;
        pos.lon = NAN;
        contact = API::SignalContact();
    }

    Subscription::Subscription(SubscriptionType t, API::SignalContact c, std::string filterString, uint priKey)
    {
        type = t;
        contact = c;
        filter = filterString;
        dbPriKey = priKey;
    }

    Message::Message(std::string& text, API::SignalContact& c)
    {
        id = nextMsgId++;
        if (nextMsgId > 0xFFFFF) nextMsgId = 0;
        msg = text;
        contact = c;
    }

    Message::Message(std::string& text, API::SignalContact& c, uint msgId)
    {
        id = msgId;
        msg = text;
        contact = c;
    }

    int Message::Send()
    {
        if (msg.length() > 67) return -1;

        Database::Query q("INSERT INTO messages (ID, Dest, ContactFID, Message) VALUES (");
        q.text += std::to_string(id) + ", '" + contact.callsign + "', " + std::to_string(contact.dbPriKey) + ", '" + msg + "') ";
        Database::Connection->Execute(q);

        SendMessage(contact.callsign, msg, id);
        return id;
    }

    void SendMessage(std::string& call, std::string& msg, uint id)
    {
        std::stringstream ss;
        ss << Config->myCall << ">APRS::" << std::setw(9) << std::left << call;
        ss << std::setw(0) << ':' << msg << '{' << std::hex << id << "\r\n";
        std::string text = ss.str();
        Connection->Send(text);
    }

    Conversation::Conversation(std::string call, API::SignalContact& c)
    {
        aprsStn = call;
        signalContact = c;
    }

    void Conversation::Add(MsgConduitType con, std::string call, API::SignalContact& c)
    {
        Conversation conv(call, c);

        Remove(con, call);
        aprsConvos.emplace(call, conv);
        signalConvos.emplace(c.id, call);
    }

    void Conversation::Remove(MsgConduitType con, std::string id)
    {
        if (con == Conduit_APRS)
        {
            auto it = aprsConvos.find(id);
            if (it == aprsConvos.end()) return;
            signalConvos.erase(it->second.signalContact.id);
            aprsConvos.erase(id);
        }
        else
        {
            auto it = signalConvos.find(id);
            if (it == signalConvos.end()) return;
            aprsConvos.erase(signalConvos.at(id));
            signalConvos.erase(id);
        }
    }

    Conversation* Conversation::Get(MsgConduitType con, std::string id)
    {
        if (con == Conduit_APRS)
        {
            if (aprsConvos.find(id) == aprsConvos.end()) return NULL;
            return &aprsConvos.at(id);
        }
        else
        {
            if (signalConvos.find(id) == signalConvos.end()) return NULL;
            return &aprsConvos.at(signalConvos.at(id));
        }
    }

    void DumpPacket(fap_packet_t* pkt)
    {
        if (pkt->error_code)
        {
            char buffer[128];
            fap_explain_error(*pkt->error_code, buffer);
            std::cout << "\tError: " << buffer << '\n';
        }
        if (pkt->src_callsign) std::cout << "\tCall: " << pkt->src_callsign << '\n';
        if (pkt->dst_callsign) std::cout << "\tToCall: " << pkt->dst_callsign << '\n';
        if (pkt->path) std::cout << "\tPath: " << aprsParser->PathToString(pkt->path, pkt->path_len) << '\n';
        if (pkt->type) std::cout << "\tType: " << aprsParser->TypeToString(*pkt->type) << '\n';
        if (pkt->latitude) std::cout << "\tLatitude: " << *pkt->latitude << '\n';
        if (pkt->longitude) std::cout << "\tLongitude: " << *pkt->longitude << '\n';
        if (pkt->altitude) std::cout << "\tAltitude: " << *pkt->altitude << '\n';
        if (pkt->course) std::cout << "\tCourse: " << *pkt->course << '\n';
        if (pkt->speed) std::cout << "\tSpeed: " << *pkt->speed << '\n';
        if (pkt->message_id) std::cout << "\tMessage ID: " << pkt->message_id << '\n';
        if (pkt->destination) std::cout << "\tMsgDest: " << pkt->destination << '\n';
        if (pkt->message) std::cout << "\tMessage: " << pkt->message << '\n';
        if (pkt->message_ack) std::cout << "\tMessage ACK: " << pkt->message_ack << '\n';
        if (pkt->message_nack) std::cout << "\tMessage NACK: " << pkt->message_nack << '\n';
        if (pkt->comment) std::cout << "\tComment: " << aprsParser->CommentToString(pkt->comment, pkt->comment_len) << '\n';
        if (pkt->object_or_item_name) std::cout << "\tObject/Item Name: " << pkt->object_or_item_name << '\n';
        if (pkt->timestamp) std::cout << "\tTimestamp: " << *pkt->timestamp << '\n';
        if (pkt->status) std::cout << "\tStatus: " << aprsParser->CommentToString(pkt->status, pkt->status_len) << '\n';
        if (pkt->wx_report) std::cout << "\tWX: " << aprsParser->WxToString(pkt->wx_report) << '\n';
        if (pkt->telemetry) std::cout << "\tTelemetry: " << aprsParser->TelemetryToString(pkt->telemetry) << '\n';
    }

    void AckMsg(fap_packet_t* msg, bool nack)
    {
        std::stringstream ack;
        ack << Config->myCall << ">APRS::" ;
        ack << std::setw(9) << std::left << msg->src_callsign << std::setw(0) << (nack ? ":rej" : ":ack") << msg->message_id << "\r\n";
        std::string text = ack.str();
        Connection->Send(text);
    }
}