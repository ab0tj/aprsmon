#include <string>
#include <cstring>
#include <iostream>
#include <thread>
#include <time.h>
#include "api.h"
#include "aprs.h"
#include "config.h"
#include "database.h"
#include "packet.h"

void PrintPacket(std::string& packet);

int main(int argc, char *argv[])
{
    BaseConfig::Config = new BaseConfig::ConfigClass(argc, argv);
    if (!BaseConfig::Config->ParseConfigFile()) return 1;

    srand(time(NULL));

    try
    {
        Database::Connection = new Database::DatabaseConnection();
    }
    catch(const std::exception& e)
    {
        return 1;
    }

    try
    {
        APRS::Connection = new APRS::ISConnection();
    }
    catch(...)
    {
        std::cerr << "APRS-IS connection failed.\n";
        return 1;
    }

    Database::Init();
    Packet::SetFilterFromMonitoredCalls();

    APRS::aprsParser = new APRS::Parser();
    if (BaseConfig::Config->verbose || BaseConfig::Config->debug) APRS::Connection->RegisterCallback(&PrintPacket);
    APRS::Connection->RegisterCallback(&Packet::Handle);
    std::thread aprsConnThread = APRS::Connection->SocketReader();

    API::Listener apiListener;
    std::thread apiListenerThread = apiListener.ListenerThread();

    std::thread packetThread(&Packet::PacketThread);

    if (API::Config->adminNotofications) API::Config->admin.Notify("aprsmon started.");

    packetThread.join();
    aprsConnThread.join();
    apiListenerThread.join();

    return 0;
}

void PrintPacket(std::string& text)
{
    if (BaseConfig::Config->debug) std::cout << '\n';
    std::cout << text;
}