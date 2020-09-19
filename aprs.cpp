#include <iostream>
#include <sstream>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <chrono>
#include <netinet/tcp.h>
#include "aprs.h"
#include "config.h"

namespace APRS
{
    ConfigClass* Config;

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
        close(sockFD);
    }

    void ISConnection::Connect()
    {
        std::string buffer;
        aprsIsStatus.state = ISStatus::Connecting;

        close(sockFD);

        struct hostent *host;
        if ((host = gethostbyname(aprsHost.c_str())) == nullptr)
        {
            std::cerr << "APRS-IS: Hostname resolution failure\n";
            aprsIsStatus.state = ISStatus::Disconnected;
            return;
        }

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

    void ISConnection::RegisterCallback(void(*cbFunc)(std::string))
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
                    for (void(*cb)(std::string) : callbacks) cb(line);
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
        if (aprsIsStatus.state == ISStatus::Connected) send(sockFD, f.c_str(), f.length(), 0);
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
        memcpy(temp, comment, len);
        temp[len] = 0;
        if (temp[len - 2] == '\r') temp[len - 2] = 0;
        if (temp[len - 1] == '\n') temp[len - 1] = 0;
        return std::string(temp);
    }

    const std::string Parser::WxToString(fap_wx_report_t* wx)
    {
        std::stringstream s;

        if (wx->wind_gust) s << "Wind Gust: " << (double)*wx->wind_gust << "m/s ";
        if (wx->wind_dir) s << "Wind Dir: " << (uint)*wx->wind_gust << "deg ";
        if (wx->wind_speed) s << "Wind Spd: " << (double)*wx->wind_speed << "m/s ";
        if (wx->temp) s << "Temp: " << (double)*wx->temp << "C ";
        if (wx->temp_in) s << "Indoor Temp: " << (double)*wx->temp_in << "C ";
        if (wx->rain_1h) s << "Rain 1h: " << (double)*wx->rain_1h << "mm ";
        if (wx->rain_24h) s << "Rain 24h: " << (double)*wx->rain_24h << "mm ";
        if (wx->rain_midnight) s << "Rain Since Midnight: " << (double)*wx->rain_midnight << "mm ";
        if (wx->humidity) s << "Humidity: " << (uint)*wx->humidity << "% ";
        if (wx->humidity_in) s << "Indoor Humidity: " << (uint)*wx->humidity_in << "% ";
        if (wx->pressure) s << "Pressure: " << (double)*wx->pressure << "mbar ";
        if (wx->luminosity) s << "Luminosity: " << (uint)*wx->luminosity << "w/m2 ";
        if (wx->snow_24h) s << "Snow 24h: " << (double)*wx->snow_24h << "mm ";
        if (wx->soft) s << "Software: " << wx->soft << ' ';

        std::string temp = s.str();
        return temp.substr(0, temp.length() - 1);
    }

    const std::string Parser::TelemetryToString(fap_telemetry_t* t)
    {
        std::stringstream s;

        s << "Seq: " << (uint)*t->seq << " ";
        s << "Values: " << (double)*t->val1 << ',' << (double)*t->val2 << ',' << (double)*t->val3;
        s << ',' << (double)*t->val4 << ',' << (double)*t->val5 << ' ';
        s << "Bits: " << t->bits[0] << t->bits[1] << t->bits[2] << t->bits[3] << t->bits[4];
        s << t->bits[5] << t->bits[6] << t->bits[7];

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
}