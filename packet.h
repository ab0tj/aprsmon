#ifndef INC_PACKET_H
#define INC_PACKET_H

#include "aprs.h"

namespace Packet
{
    void Handle(std::string& text);
    void AddMonitoredStation(APRS::Station& s, bool init = false);
    void DelMonitoredStation(std::string s);
    void SetFilterFromMonitoredCalls();
    void PacketThread();
}

#endif