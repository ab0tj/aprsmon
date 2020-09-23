#include <iostream>
#include <string.h>
#include <time.h>
#include "database.h"
#include "config.h"
#include "packet.h"

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

    void DatabaseConnection::Execute(Query& q)
    {
        if (mysql_query(mysql, q.text.c_str()))
        {
            std::cerr << "Database: Query error (" << q.text << "): " << mysql_error(mysql) << '\n';
            return;
        }
        mysql_free_result(q.result);
        q.result = mysql_store_result(mysql);
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

    int LoadMonitoredStations()
    {
        const std::string text = "SELECT PriKey, Callsign, LastHeard, LastDigi, LastIgate, "
                                "Latitude, Longitude, MonitorFlags FROM stations";
        Query q = Query(text);
        Connection->Execute(q);

        int numStations = 0;

        while (MYSQL_ROW row = mysql_fetch_row(q.result))
        {
            numStations++;

            APRS::Station s = APRS::Station(row[1]);
            s.dbPriKey = std::stoi(row[0]);
            if (row[2]) s.LastHeard = FieldToTime(row[2]);
            if (row[3]) s.LastDigi = FieldToTime(row[3]);
            if (row[4]) s.LastIgate = FieldToTime(row[4]);
            if (row[5] && row[6])
            {
                s.pos.lat = std::stof(row[5]);
                s.pos.lon = std::stof(row[6]);
            }
            s.monitorFlags = std::stoi(row[7]);

            Packet::AddMonitoredStation(s, true);
        }

        return numStations;
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