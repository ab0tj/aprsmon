#include <iostream>
#include <cstdlib>
#include <thread>
#include <chrono>
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

    void AddMonitoredStation(APRS::Station& s, bool init)
    {
        if (BaseConfig::Config->debug)
        {
            std::cout << "Packet: Monitoring station " << s.call;
            std::cout << " flags " << s.monitorFlags << '\n';
        }
        monitoredStations.emplace(s.call, s);
        if (!init) SetFilterFromMonitoredCalls();
    }

    void DelMonitoredStation(std::string s)
    {
        monitoredStations.erase(s);
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

    void Message(APRS::Packet& p)
    {
        if (p.packet->message_ack || p.packet->message_nack)
        {
            /* This was an ACK or NACK to an existing message */
            bool nack = (p.packet->message_nack);
            uint id;
            if (nack) id = strtoul(p.packet->message_nack, NULL, 16);
            else id = strtoul(p.packet->message_ack, NULL, 16);

            Database::Query q("SELECT SignalID, IsGroup, messages.Dest, messages.Message from contacts "
                            "INNER JOIN messages ON contacts.PriKey = messages.ContactFID WHERE messages.ID = " + std::to_string(id));
            
            if (Database::Connection->Execute(q) > 0)
            {
                MYSQL_ROW row = mysql_fetch_row(q.result);
                API::SignalContact c(row[0], std::stoi(row[1]));
                std::string text = "Your message: \"";
                text += row[3];
                text += "\"\nwas ";
                text += (nack ? "rejected by " : "delivered to ");
                text += row[2];
                c.Notify(text);
            }

            q.text = "UPDATE messages SET ID = NULL WHERE ID = " + std::to_string(id);
            Database::Connection->Execute(q);

            return;
        }

        /* This is an incoming message, process it. */
        std::string input(p.packet->message);
        std::string src = p.packet->src_callsign;
        std::string dest;
        char destType = input[0];
        API::SignalContact c;
        bool ack = false;

        if (destType == '+' || destType == '@')
        {
            size_t space = input.find_first_of(' ');
            dest = input.substr(1, space - 1);
            std::string msg = input.substr(space + 1);

            std::string text = std::string(src) + " says:\n" + msg;

            if (destType == '+')
            {
                c.id = '+' + dest;
                c.group = false;
                ack = c.Notify(text);
            }
            else if (p.packet->message[0] == '@')
            {
                c = API::ContactLookup(dest, src);
                if (c.id.size() != 0)
                {
                    ack = c.Notify(text);
                }
            }

            if (ack)
            {
                c.Notify("You are now in a conversation with " + src + ". Send !@ <message> to reply or !help for more information.\n");
                APRS::Conversation::Add(APRS::Conduit_APRS, src, c);
            }
        }
        else
        {
            APRS::Conversation* conv = APRS::Conversation::Get(APRS::Conduit_APRS, src);
            if (conv != NULL)
            {
                dest = conv->aprsStn;
                c = conv->signalContact;
                ack = c.Notify(std::string(src) + " says:\n" + input);
            }
        }

        APRS::AckMsg(p.packet, !ack);
    }

    void Handle(std::string& text)
    {
        APRS::Packet p = APRS::Packet(text);
        APRS::aprsParser->Parse(p);
        if (p.packet->error_code) return;

        if (BaseConfig::Config->debug) APRS::DumpPacket(p.packet);  /* Show packet details */

        if (monitoredStations.find(p.packet->src_callsign) != monitoredStations.end())
        {
            /* Update info if this is a station we monitor */
            APRS::Station& s = monitoredStations.at(p.packet->src_callsign);
            s.lastHeard = time(NULL);

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
            /* Check if this was digi'd or igated by a monitored station */
            std::string stn(p.packet->path[i]);
            if (stn[0] == 'q' && stn[1] == 'A' && (stn[2] == 'r' || stn[2] == 'R') && stn.size() == 3) igate = i + 1;
            if (i != igate && monDigiStations.find(stn) != monDigiStations.end())
            {
                APRS::Station& s = monitoredStations.at(stn);
                s.lastDigi = time(NULL);
                if (BaseConfig::Config->debug) std::cout << "Packet: This was digipeated by " << s.call << '\n';
                UpdateStationDb(s);
            }
            else if (i == igate && monIgateStations.find(stn) != monDigiStations.end())
            {
                APRS::Station& s = monitoredStations.at(stn);
                s.lastIgate = time(NULL);
                if (BaseConfig::Config->debug) std::cout << "Packet: This was igated by " << s.call << '\n';
                UpdateStationDb(s);
            }
        }

        if (*p.packet->type == fapMESSAGE && strcmp(p.packet->destination, APRS::Config->myCall.c_str()) == 0)
        {
            /* This was a message to aprsmon */
            Message(p);
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
    }

    void UpdateStationDb(APRS::Station& s)
    {
        Database::Query q("UPDATE stations SET LastHeard = ");
        q.text += Database::FormatTime(&s.lastHeard);
        q.text += ", LastDigi = " + Database::FormatTime(&s.lastDigi);
        q.text += ", LastIgate = " + Database::FormatTime(&s.lastIgate);
        q.text += ", Latitude = " + (std::isnan(s.pos.lat) ? "NULL" : std::to_string(s.pos.lat));
        q.text += ", Longitude = " + (std::isnan(s.pos.lon) ? "NULL" : std::to_string(s.pos.lon));
        q.text += ", MonitorFlags = " + std::to_string(s.monitorFlags.to_ulong());
        q.text += ", StateFlags = " + std::to_string(s.stateFlags.to_ulong());
        q.text += " WHERE PriKey = " + std::to_string(s.dbPriKey);
        Database::Connection->Execute(q);
    }

    bool CheckWatchdog(const std::string call, APRS::subscription_bits& bits, time_t last, time_t timeout, const std::string deadMsg,
                        const std::string aliveMsg, std::vector<APRS::Subscription>& subscriptions, APRS::SubscriptionType type)
    {
        time_t now = time(NULL);
        bool isDead = (now - last > 60*60);   /* Watchdog timed out after 1h */
        bool stateChange = (isDead != bits[type]);
        if (stateChange)
        {
            bits[type] = isDead;
            std::string text = "Station " + call + ' ';
            if (isDead) text += deadMsg;
            else text += aliveMsg;
            if (BaseConfig::Config->debug) std::cout << "Packet: " << text << '\n';

            for (APRS::Subscription sub : subscriptions)
            {
                /* Send notification to any users that are subscribed for this station */
                if (sub.type == type) sub.contact.Notify(text);
            }
        }
        return stateChange;
    }

    void RetryPendingMessages()
    {
        MYSQL_ROW row;
        /* Check for pending messages */
        Database::Query q("SELECT messages.PriKey, ID, Dest, Message, contacts.SignalID, contacts.IsGroup, "
                        "TIMESTAMPDIFF(SECOND, Sent, NOW()) AS Age "
                        "FROM messages INNER JOIN contacts ON contacts.PriKey = messages.ContactFID "
                        "WHERE ID IS NOT NULL AND Sent < now() - INTERVAL 1 MINUTE");
        if (Database::Connection->Execute(q) > 0)
        {
            while ((row = mysql_fetch_row(q.result)) != NULL)
            {
                int age = std::stoi(row[6]);

                if (age > 60*10) /* Timed out */
                {
                    API::SignalContact c(row[4], std::stoi(row[5]));
                    std::string text("Your message: \"");
                    text += row[3];
                    text += "\"\nto ";
                    text += row[2];
                    text += " has timed out.";
                    c.Notify(text);

                    Database::Query subQ("UPDATE messages SET ID = NULL WHERE PriKey = ");
                    subQ.text += row[0];
                    Database::Connection->Execute(subQ);
                }
                else /* Still pending, retry. */
                {
                    std::string dest(row[2]);
                    std::string msg(row[3]);
                    APRS::SendMessage(dest, msg, std::stoi(row[1]));
                }
            }
        }
    }

    void PacketThread()
    {
        uint warmup = 0;
        for (;;)
        {
            if (warmup++ >= 60)
            {
                if(BaseConfig::Config->debug) std::cout << "Packet: Running watchdog check.\n";
                for (auto pair : monitoredStations)
                {
                    int stateChanges = 0;
                    /* Check if watchdog status changed for any monitored station */
                    APRS::Station& stn = monitoredStations.at(pair.first);
                    stateChanges += CheckWatchdog(stn.call, stn.stateFlags, stn.lastHeard, 60*60,
                                "has not been heard in the last 60 minutes.", "is back online.",
                                stn.subscriptions, APRS::SubscriptionType::Watchdog);
                    stateChanges += CheckWatchdog(stn.call, stn.stateFlags, stn.lastDigi, 60*60,
                                "has not digipeated in the last 60 minutes.", "is digipeating.",
                                stn.subscriptions, APRS::SubscriptionType::Digi);
                    stateChanges += CheckWatchdog(stn.call, stn.stateFlags, stn.lastIgate, 60*60,
                                "has not igated in the last 60 minutes.", "is igating.",
                                stn.subscriptions, APRS::SubscriptionType::Igate);

                    if (stateChanges > 0) UpdateStationDb(stn);
                }
            }
            else warmup++;

            Database::Query q("DELETE FROM packets WHERE Heard < now() - INTERVAL " + std::to_string(Database::Config->keep_days) + " DAY");
            int rows = Database::Connection->Execute(q);
            if (BaseConfig::Config->debug && rows > 0) std::cout << "Removed " << rows << " old rows from packets table\n";

            q.text = "DELETE FROM messages WHERE Sent < now() - INTERVAL " + std::to_string(Database::Config->keep_days) + " DAY";
            rows = Database::Connection->Execute(q);
            if (BaseConfig::Config->debug && rows > 0) std::cout << "Removed " << rows << " old rows from messages table\n";

            RetryPendingMessages();

            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }
}