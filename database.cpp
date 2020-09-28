#include <iostream>
#include <string.h>
#include <time.h>
#include "database.h"
#include "config.h"
#include "packet.h"
#include "api.h"

namespace Database
{
    ConfigClass* Config;
    DatabaseConnection* Connection;

    DatabaseConnection::DatabaseConnection()
    {
        mysql = mysql_init(NULL);
        mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, BaseConfig::versString.c_str());
        if (mysql_real_connect(mysql, Config->dbHost.c_str(), Config->dbUser.c_str(), Config->dbPassword.c_str(),
            Config->dbName.c_str(), Config->dbPort, Config->dbSocket.c_str(), 0) == NULL)
        {
            std::cerr << "Database connection failed: " << mysql_error(mysql) << '\n';
            throw std::exception();
        }
    }

    DatabaseConnection::DatabaseConnection(std::string host, uint port, std::string user, std::string password, std::string database, std::string socket)
    {
        mysql = mysql_init(NULL);
        mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, BaseConfig::versString.c_str());
        if (!mysql_real_connect(mysql, host.c_str(), user.c_str(), password.c_str(), database.c_str(), port, socket.c_str(), 0))
        {
            throw std::exception();
        }
    }

    DatabaseConnection::~DatabaseConnection()
    {
        mysql_close(mysql);
    }

    int DatabaseConnection::Execute(Query& q)
    {
        mysql_free_result(q.result);
        if (mysql_query(mysql, q.text.c_str()))
        {
            std::cerr << "Database: Query error (" << q.text << "): " << mysql_error(mysql) << '\n';
            return -1;
        }
        q.result = mysql_store_result(mysql);

        if (BaseConfig::Config->debug) std::cout << "Database: Query \"" << q.text << "\" afftected " << mysql_affected_rows(mysql) << " rows.\n";

        return mysql_affected_rows(mysql);
    }

    Query::Query()
    {
        text = std::string();
        result = nullptr;
    }

    Query::Query(const std::string queryText)
    {
        text = queryText;
        result = nullptr;
    }

    Query::~Query()
    {
        mysql_free_result(result);
    }

    time_t FieldToTime(char* field)
    {
        struct tm tm;
        strptime(field, "%Y-%m-%d %H:%M:%S", &tm);
        time_t t = mktime(&tm);
        return t;
    }

    int Init()
    {
        Query q = Query("SELECT stations.PriKey, stations.Callsign, "
                                "stations.LastHeard, stations.LastDigi, stations.LastIgate, "
                                "stations.Latitude, stations.Longitude, stations.MonitorFlags, "
                                "stations.StateFlags, contacts.SignalID, contacts.IsGroup FROM stations "
                                "LEFT OUTER JOIN contacts ON stations.ContactFID=contacts.PriKey");
        Connection->Execute(q);

        int numStations = 0;

        while (MYSQL_ROW row = mysql_fetch_row(q.result))
        {
            numStations++;

            APRS::Station s = APRS::Station(row[1]);
            s.dbPriKey = std::stoi(row[0]);

            if (row[2]) s.lastHeard = FieldToTime(row[2]);
            if (row[3]) s.lastDigi = FieldToTime(row[3]);
            if (row[4]) s.lastIgate = FieldToTime(row[4]);

            if (row[5] && row[6])
            {
                s.pos.lat = std::stof(row[5]);
                s.pos.lon = std::stof(row[6]);
            }
            s.monitorFlags = std::stoi(row[7]);
            s.stateFlags = std::stoi(row[8]);

            if (row[9] && row[10])
            {
                s.contact.id = row[9];
                s.contact.group = atoi(row[10]);
            }

            Query subQ("SELECT subscriptions.PriKey, subscriptions.Type, subscriptions.Filter, contacts.SignalID, contacts.IsGroup "
                    "FROM subscriptions INNER JOIN contacts ON subscriptions.ContactFID=contacts.PriKey " 
                    "WHERE subscriptions.StationFID = ");
            subQ.text += std::to_string(s.dbPriKey);
            Connection->Execute(subQ);

            while (MYSQL_ROW row = mysql_fetch_row(subQ.result))
            {
                API::SignalContact c(row[3], atoi(row[4]));
                APRS::Subscription sub((APRS::SubscriptionType)atoi(row[1]), c, (row[2] ? std::string(row[2]) : ""), atoi(row[0]));
                s.subscriptions.push_back(sub);
            }
            if (BaseConfig::Config->debug) std::cout << "Station " << s.call << " has " << s.subscriptions.size() << " subscriptions.\n";

            Packet::AddMonitoredStation(s, true);
        }

        return numStations;
    }

    void DelMonitoredStation(APRS::Station& s)
    {
        /* Delete from stations, and any associated packets */
    }

    const std::string FormatTime(const time_t* t)
    {
        if (*t != 0)
        {
            char buff[20];
            strftime(buff, 20, "%Y-%m-%d %H:%M:%S", gmtime(t));
            return std::string('\'' + std::string(buff) + '\'');
        }
        else return "NULL";
    }
}