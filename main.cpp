#include <string>
#include <cstring>
#include <iostream>
#include <thread>
#include "api.h"
#include "aprs.h"
#include "config.h"

void PrintPacket(std::string packet);
APRS::Parser* aprsParser;

int main(int argc, char *argv[])
{
    BaseConfig::Config = new BaseConfig::ConfigClass(argc, argv);
    if (!BaseConfig::Config->ParseConfigFile()) return 1;
    
    APRS::ISConnection *aprsConn;
    try
    {
        aprsConn = new APRS::ISConnection(APRS::Config->aprsHost, APRS::Config->aprsPort, APRS::Config->myCall, APRS::Config->passCode);
    }
    catch(...)
    {
        std::cerr << "APRS-IS connection failed.\n";
        return 1;
    }

    aprsParser = new APRS::Parser();
    aprsConn->RegisterCallback(&PrintPacket);
    std::thread aprsReader = aprsConn->SocketReader();
    aprsConn->SetFilter("b/AB0TJ/AB0JT-13/CEDAR/MAGMTN/BIGJ d/AB0TJ/CEDAR/MAGMTN/BIGJ e/AB0TJ/CEDAR/MAGMTN/BIGJ");

    API::Listener apiListener(API::Config->listenIP, API::Config->listenPort);
    std::thread apiListenerThread = apiListener.ListenerThread();

    aprsReader.join();
    apiListenerThread.join();

    return 0;
}

void PrintPacket(std::string text)
{
    std::cout << text;
    fap_packet_t* fap;

    if (BaseConfig::Config->debug)
    {
        APRS::Packet p = APRS::Packet(text);
        aprsParser->Parse(p);
        fap = p.packet;

        if (fap->error_code)
        {
            char buffer[128];
            fap_explain_error(*fap->error_code, buffer);
            std::cout << "\tError: " << buffer << '\n';
        }
        if (fap->src_callsign) std::cout << "\tCall: " << fap->src_callsign << '\n';
        if (fap->dst_callsign) std::cout << "\tToCall: " << fap->dst_callsign << '\n';
        if (fap->path) std::cout << "\tPath: " << aprsParser->PathToString(fap->path, fap->path_len) << '\n';
        if (fap->type) std::cout << "\tType: " << aprsParser->TypeToString(*fap->type) << '\n';
        if (fap->latitude) std::cout << "\tLatitude: " << *fap->latitude << '\n';
        if (fap->longitude) std::cout << "\tLongitude: " << *fap->longitude << '\n';
        if (fap->altitude) std::cout << "\tAltitude: " << *fap->altitude << '\n';
        if (fap->course) std::cout << "\tCourse: " << *fap->course << '\n';
        if (fap->speed) std::cout << "\tSpeed: " << *fap->speed << '\n';
        if (fap->message_id) std::cout << "\tMessage ID: " << fap->message_id << '\n';
        if (fap->destination) std::cout << "\tMsgDest: " << fap->destination << '\n';
        if (fap->message) std::cout << "\tMessage: " << fap->message << '\n';
        if (fap->message_ack) std::cout << "\tMessage ACK: " << fap->message_ack << '\n';
        if (fap->message_nack) std::cout << "\tMessage NACK: " << fap->message_nack << '\n';
        if (fap->comment) std::cout << "\tComment: " << aprsParser->CommentToString(fap->comment, fap->comment_len) << '\n';
        if (fap->object_or_item_name) std::cout << "\tObject/Item Name: " << fap->object_or_item_name << '\n';
        if (fap->timestamp) std::cout << "\tTimestamp: " << *fap->timestamp << '\n';
        if (fap->status) std::cout << "\tStatus: " << aprsParser->CommentToString(fap->status, fap->status_len) << '\n';
        if (fap->wx_report) std::cout << "\tWX: " << aprsParser->WxToString(fap->wx_report) << '\n';
        if (fap->telemetry) std::cout << "\tTelemetry: " << aprsParser->TelemetryToString(fap->telemetry) << '\n';
        std::cout << '\n';
    }
}