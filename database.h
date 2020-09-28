#ifndef INC_DATABASE_H
#define INC_DATABASE_H

#include <string>
#include <vector>
#include <mariadb/mysql.h>
#include "aprs.h"

namespace Database
{
    class ConfigClass
    {
        public:
            std::string dbHost;
            uint dbPort;
            std::string dbUser;
            std::string dbPassword;
            std::string dbName;
            std::string dbSocket;
            uint keep_days;
    };

    extern ConfigClass* Config;

    class Query
    {
        public:
            Query();
            Query(const std::string text);
            ~Query();
            std::string text;
            MYSQL_RES* result;
    };

    class DatabaseConnection
    {
        public:
            DatabaseConnection();
            DatabaseConnection(std::string host, uint port, std::string user, std::string password, std::string database, std::string socket);
            ~DatabaseConnection();
            int Execute(Query& q);

        private:
            MYSQL* mysql;
    };

    int Init();
    void AddMonitoredStation(APRS::Station& s);
    void DelMonitoredStation(APRS::Station& s);
    const std::string FormatTime(const time_t* t);

    extern DatabaseConnection* Connection;
}

#endif