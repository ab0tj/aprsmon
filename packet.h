#ifndef INC_PACKET_H
#define INC_PACKET_H

#include "aprs.h"

namespace Packet
{
    void Handle(std::string& text);
    void AddMonitoredStation(APRS::Station s, bool init = false);
    void SetFilterFromMonitoredCalls();
}

#endif