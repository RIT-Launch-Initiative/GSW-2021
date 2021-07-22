#include <stdio.h>
#include <csignal>
#include "lib/nm/nm.h"
#include "lib/shm/shm.h"
#include "lib/dls/dls.h"
#include "lib/vcm/vcm.h"
#include "lib/telemetry/TelemetryShm.h"
#include "common/types.h"
#include <csignal>
#include <string>
#include <unistd.h>
#include <sys/types.h>

using namespace dls;
using namespace vcm;
using namespace nm;
using namespace shm;


VCM* veh = NULL;

NetworkManager* net = NULL;
std::string decom_id = "main"; // id for each decom proc spawned

bool child_proc = false;
std::vector<pid_t> pids;

bool ignore_kill = false;


void sighandler(int signum) {
    MsgLogger logger("DECOM");

    if(ignore_kill) {
        logger.log_message("attempt to kill decom " + decom_id + ", ignoring");
        return; // ignored
    }

    logger.log_message("decom " + decom_id + " killed, cleaning up resources");

    if(net) {
        delete net; // this also calls close
    }

    if(!child_proc) { // if we're the main process, kill all the children :(
        for(pid_t pid : pids) {
            if(-1 == kill(pid, SIGTERM)) {
                logger.log_message("Failed to kill child with PID: " + std::to_string(pid));
                // ignore and kill the other children
            }
        }
    } else { // if we're a child we need to clean up our mess
        if(net) { // it's possible we get killed before we can create out network manager
            delete net;
        }
    }

    exit(signum);
}

// main logic for each sub process to run
// only exit if something bad happens
void execute(size_t packet_id, packet_info_t* packet) {
    std::string decom_id = std::to_string(packet_id);

    // create message logger
    std::string func_name = "execute: ";
    func_name += decom_id;
    MsgLogger logger("DECOM", func_name.c_str());

    // set packet name to use for logging messages and network manager name
    std::string packet_name = veh->device + "[" + std::to_string(packet_id) + "]";

    // create a receive buffer
    uint8_t* buffer = new uint8_t[packet->size];

    // open the network manager (use default timeout)
    net = new NetworkManager(packet->port, packet_name.c_str(), buffer, packet->size);
    if(FAILURE == net->Open()) {
        logger.log_message("failed to open network manager");
        return;
    }

    // open shared memory
    TelemetryShm shmem;
    if(shmem.init(veh) == FAILURE) {
        logger.log_message("failed to init telemetry shared memory");
        return;
    }

    // attach to shared memory
    if(shmem.open() == FAILURE) {
        logger.log_message("failed to attach to telemetry shared memory");
        return;
    }

    // clear our packet's shared memory
    if(shmem.clear(packet_id) == FAILURE) {
        logger.log_message("failed to clear telemetry shared memory");
    }

    // create packet logger
    PacketLogger plogger(packet_name);

    // main loop
    size_t n = 0;
    while(1) {
        // send any outgoing messages
        net->Send(); // don't care about the return

        // read any incoming message and write it to shared memory
        if(SUCCESS == net->Receive(&n)) {
            if(n != packet->size) {
                logger.log_message("Packet size mismatch, " + std::to_string(packet->size) +
                                   " != " + std::to_string(n) + " (received)");
            } else { // only write the packet to shared mem if it's the correct size
                // mem.write_to_shm((void*)net->in_buffer, net->in_size);
                if(shmem.write(packet_id, buffer) == FAILURE) {
                    logger.log_message("failed to write packet to shared memory");
                    // ignore and continue
                }
            }
            plogger.log_packet((unsigned char*)buffer, n); // log the packet either way
        }
    }

}

int main(int argc, char** argv) {
    // interpret the 1st argument as a config_file location if available
    std::string config_file = "";
    if(argc > 1) {
        config_file = argv[1];
    }

    // can't catch sigkill or sigstop though
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGSEGV, sighandler);
    signal(SIGFPE, sighandler);
    signal(SIGABRT, sighandler);

    if(config_file == "") {
        veh = new VCM(); // use default config file
    } else {
        veh = new VCM(config_file); // use specified config file
    }

    MsgLogger logger("DECOM", "main");
    logger.log_message("starting decom sub-processes");

    // according to packets in vcm, spawn a bunch of processes
    // we don't want to killed in the process of making these children so we ignore kill signals
    ignore_kill = true;

    pid_t pid;
    size_t i = 0;
    for(packet_info_t* packet : veh->packets) {
        pid = fork();
        if(pid == -1) {
            logger.log_message("failed to start decom sub-process " + std::to_string(i));
        } else if(pid == 0) { // we are the child
            // if we allowed killing during this step, the child process could be killed before it's able to set the 'child_proc' boolean
            // which would make it try and clean up other children on kill, and we dont want multiple processes trying to kill each other
            child_proc = true;
            ignore_kill = false;
            execute(i, packet);

            return -1; // if a child returns, something bad happened to it and it should exit
        }

        // otherwise we're the parent, keep going
        logger.log_message("started decom sub-process " + std::to_string(i));
        i++;
        pids.push_back(pid);
    }

    ignore_kill = false;

    // TODO wait loop here

    return 1;
}
