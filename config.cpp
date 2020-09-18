#include <iostream>
#include <getopt.h>
#include "inih/cpp/INIReader.h"
#include "config.h"
#include "api.h"
#include "aprs.h"

namespace BaseConfig
{
    ConfigClass* Config;

    ConfigClass::ConfigClass(int argc, char *argv[])
    {
        API::Config = new API::ConfigClass();
        APRS::Config = new APRS::ConfigClass();

        char opt;

        configFile = "/etc/aprsmon.conf";

        while ((opt = getopt(argc, argv, "fvc:")) != -1)
        {
            switch (opt)
            {
                case 'f':
                    foreground = true;
                    break;

                case 'v':
                    verbose = true;
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
            std::cout << "Can't load '" << configFile << "'\n";
            return false;
        }

        Config->dbFile = reader.GetString("main", "db-file", "/etc/aprsmon.db");
        APRS::Config->myCall = reader.GetString("aprs", "mycall", "N0CALL");
        APRS::Config->passCode = reader.GetInteger("aprs", "passcode", -1);
        APRS::Config->aprsHost = reader.GetString("aprs", "is-server", "rotate.aprs2.net");
        APRS::Config->aprsPort = reader.GetInteger("aprs", "is-port", 14580);
        API::Config->listenIP = reader.GetString("api", "listen-ip", "127.0.0.1");
        API::Config->listenPort = reader.GetInteger("api", "listen-port", 9999);
        API::Config->signalBotHost = reader.GetString("api", "signalbot-host", "127.0.0.1");
        API::Config->signalBotPort = reader.GetInteger("api", "signalbot-port", 9998);

        return true;
    }
}