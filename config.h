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
            bool foreground;
            ConfigClass(int argc, char *argv[]);
            bool ParseConfigFile();

        private:
            std::string configFile;
    };

    extern BaseConfig::ConfigClass* Config;
}

#endif