#ifndef INC_CONFIG_H
#define INC_CONFIG_H

#include <string>

namespace BaseConfig
{
    class ConfigClass
    {
        public:
            std::string dbFile;
            bool verbose;
            bool debug;
            bool foreground;
            ConfigClass(int argc, char *argv[]);
            bool ParseConfigFile();

        private:
            std::string configFile;
    };

    extern BaseConfig::ConfigClass* Config;
    const std::string versString = "AprsMon 0.1";
}

#endif