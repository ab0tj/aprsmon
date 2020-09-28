#include <iostream>
#include <getopt.h>
#include "inih/cpp/INIReader.h"
#include "config.h"
#include "api.h"
#include "aprs.h"
#include "database.h"

namespace BaseConfig
{
    ConfigClass* Config;

    ConfigClass::ConfigClass(int argc, char *argv[])
    {
        API::Config = new API::ConfigClass();
        APRS::Config = new APRS::ConfigClass();
        Database::Config = new Database::ConfigClass();

        char opt;

        configFile = "/etc/aprsmon.conf";

        while ((opt = getopt(argc, argv, "fvdc:")) != -1)
        {
            switch (opt)
            {
                case 'f':
                    foreground = true;
                    break;

                case 'v':
                    verbose = true;
                    break;

                case 'd':
                    debug = true;
                    break;

                case 'c':
                    configFile = optarg;
                    break;
            }
        }
    }

   bool ConfigClass::ParseConfigFile()
    {
        INIReader reader(configFile);

        if (reader.ParseError() < 0)
        {
            std::cerr << "Can't load '" << configFile << "'\n";
            return false;
        }

        Config->dbFile = reader.GetString("main", "db-file", "/etc/aprsmon.db");
        APRS::Config->myCall = reader.GetString("aprs", "mycall", "N0CALL");
        APRS::Config->passCode = reader.GetString("aprs", "passcode", "-1");
        APRS::Config->aprsHost = reader.GetString("aprs", "is-server", "rotate.aprs2.net");
        APRS::Config->aprsPort = reader.GetString("aprs", "is-port", "14580");
        API::Config->listenIP = reader.GetString("api", "listen-ip", "127.0.0.1");
        API::Config->listenPort = reader.GetInteger("api", "listen-port", 9999);
        API::Config->signalBotHost = reader.GetString("api", "signalbot-host", "127.0.0.1");
        API::Config->signalBotPort = reader.GetInteger("api", "signalbot-port", 9998);
        API::Config->adminNotofications = reader.GetBoolean("api", "admin_notifications", false);
        API::Config->admin.id = reader.GetString("api", "admin_contact_id", "");
        API::Config->admin.group = reader.GetBoolean("api", "admin_contact_group", false);
        Database::Config->dbHost = reader.GetString("database", "host", "127.0.0.1");
        Database::Config->dbPort = reader.GetInteger("database", "port", 3306);
        Database::Config->dbSocket = reader.GetString("database", "socket", "/var/run/mysqld/mysqld.sock");
        Database::Config->dbName = reader.GetString("database", "name", "aprsmon");
        Database::Config->dbUser = reader.GetString("database", "user", "aprsmon");
        Database::Config->dbPassword = reader.GetString("database", "pass", "");
        Database::Config->keep_days = reader.GetInteger("database", "keep_days", 30);

        return true;
    }
}