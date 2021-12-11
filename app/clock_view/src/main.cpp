/*******************************************************************************
* Name: main.cpp
*
* Purpose: Countdown clock viewer application
*
* Author: Will Merges
*
* RIT Launch Initiative
*******************************************************************************/
#include "lib/clock/clock.h"
#include "lib/dls/dls.h"
#include <stdio.h>
#include <unistd.h>

using namespace countdown_clock;
using namespace dls;

int main() {
    MsgLogger logger("CLOCK_VIEW", "main");

    CountdownClock cl;

    if(FAILURE == cl.init()) {
        logger.log_message("Failed to init clock");
        printf("Failed to init clock\n");

        return -1;
    }

    if(FAILURE == cl.open()) {
        logger.log_message("Failed to open clock");
        printf("Failed to open clock\n");

        return -1;
    }

    int64_t t_time;
    int64_t hold_time;
    bool hold_set;
    while(1) {
        if(FAILURE != cl.read_time(&t_time, &hold_time, &hold_set)) {
            // clear the screen
            printf("\033[2J");

            // print the times
            printf("time: %ld\n", t_time);
            if(hold_set) {
                printf("hold: %ld\n", hold_time);
            } else {
                printf("hold: -----\n");
            }
        }

        // sleep 1 ms
        usleep(1000);
    }
}
