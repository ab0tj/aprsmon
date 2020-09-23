#include <iostream>
#include "packet.h"
#include "config.h"
#include "database.h"

namespace Packet
{
    std::unordered_map<std::string, APRS::Station> monitoredStations;
    std::unordered_map<std::string, int> monDigiStations;
    std::unordered_map<std::string, int> monIgateStations;
    void WritePacketToDb(APRS::Packet& p);
    void UpdateStationDb(APRS::Station& s);
    void StorePacket(APRS::Station& s, APRS::Packet& p);

    void AddMonitoredStation(APRS::Station s, bool init)
    {
        if (BaseConfig::Config->debug)
        {
            std::cout << "Packet: Monitoring station " << s.call;
            std::cout << " flags " << s.monitorFlags << '\n';
        }
        monitoredStations.emplace(s.call, s);
        if (!init) SetFilterFromMonitoredCalls();
    }

    void SetFilterFromMonitoredCalls()
    {
        std::string filter = "b";
        std::string dCalls = "d";
        std::string eCalls = "e";

        for (auto s : monitoredStations)
        {
            filter += '/' + s.first;

            if (s.second.monitorFlags[APRS::SubscriptionType::Digi])
            {
                dCalls += '/' + s.first;
                monDigiStations.emplace(s.first, 0);
            }

            if (s.second.monitorFlags[APRS::SubscriptionType::Igate])
            {
                eCalls += '/' + s.first;
                monIgateStations.emplace(s.first, 0);
            }
        }

        if (dCalls.size() > 1) filter += ' ' + dCalls;
        if (eCalls.size() > 1) filter += ' ' + eCalls;

        APRS::Connection->SetFilter(filter);
    }

    void Handle(std::string& text)
    {
        APRS::Packet p = APRS::Packet(text);
        APRS::aprsParser->Parse(p);
        if (BaseConfig::Config->debug) APRS::DumpPacket(p.packet);

        if (monitoredStations.find(p.packet->src_callsign) != monitoredStations.end())
        {
            APRS::Station& s = monitoredStations.at(p.packet->src_callsign);
            s.LastHeard = time(NULL);

            if (*p.packet->type == fapLOCATION)
            {
                if (p.packet->latitude) s.pos.lat = *p.packet->latitude;
                if (p.packet->longitude) s.pos.lon = *p.packet->longitude;
            }

            UpdateStationDb(s);
            StorePacket(s, p);
        }

        uint igate = p.packet->path_len + 1;
        for (uint i = 0; i < p.packet->path_len; i++)
        {
            std::string stn(p.packet->path[i]);
            if (stn[0] == 'q' && stn[1] == 'A' && (stn[2] == 'r' || stn[2] == 'R') && stn.size() == 3) igate = i + 1;
            if (i != igate && monDigiStations.find(stn) != monDigiStations.end())
            {
                APRS::Station& s = monitoredStations.at(stn);
                s.LastDigi = time(NULL);
                UpdateStationDb(s);
            }
            else if (i == igate && monIgateStations.find(stn) != monDigiStations.end())
            {
                APRS::Station& s = monitoredStations.at(stn);
                s.LastIgate = time(NULL);
                UpdateStationDb(s);
            }
        }
    }

    void StorePacket(APRS::Station& s, APRS::Packet& p)
    {
        Database::Query q("INSERT INTO packets (StationFID, Type, Packet) VALUES(");
        q.text += std::to_string(s.dbPriKey);
        q.text += ',' + std::to_string(*p.packet->type);
        char buff[(p.text.size() * 2) + 1];
        mysql_hex_string(buff, p.text.c_str(), p.text.size());
        q.text += ",0x" + std::string(buff) + ')';
        Database::Connection->Execute(q);

        q.text = "DELETE FROM packets WHERE Heard < now() - INTERVAL " + std::to_string(Database::Config->keep_days) + " DAY";
        Database::Connection->Execute(q);
    }

    void UpdateStationDb(APRS::Station& s)
    {
        Database::Query q("UPDATE stations SET LastHeard = ");
        q.text += Database::FormatTime(&s.LastHeard);
        q.text += ", LastDigi = " + Database::FormatTime(&s.LastDigi);
        q.text += ", LastIgate = " + Database::FormatTime(&s.LastIgate);
        q.text += ", Latitude = " + (std::isnan(s.pos.lat) ? "NULL" : std::to_string(s.pos.lat));
        q.text += ", Longitude = " + (std::isnan(s.pos.lon) ? "NULL" : std::to_string(s.pos.lon));
        q.text += " WHERE PriKey = " + std::to_string(s.dbPriKey);
        Database::Connection->Execute(q);
    }
}