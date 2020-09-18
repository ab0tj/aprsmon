#include <string>
#include "api.h"
#include "aprs.h"
#include "config.h"

int main(int argc, char *argv[])
{
    BaseConfig::Config = new BaseConfig::ConfigClass(argc, argv);

    return 0;
}