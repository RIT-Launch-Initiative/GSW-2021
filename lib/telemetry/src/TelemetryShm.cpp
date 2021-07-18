/*******************************************************************************
* Name: TelemetryShm.cpp
*
* Purpose: Telemetry shared memory access and control for readers and writers
*
* Author: Will Merges
*
* RIT Launch Initiative
*******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <limits.h>
#include "lib/telemetry/TelemetryShm.h"

// locking is done with writers preference
// https://en.wikipedia.org/wiki/Readers%E2%80%93writers_problem

// P and V semaphore macros
#define P(X) \
    if(0 != sem_wait( &( (X) ) )) { \
        return FAILURE; \
    } \

#define V(X) \
    if(0 != sem_post( &( (X) ) )) { \
        return FAILURE; \
    } \

// initialize semaphore macro
#define INIT(X, V) \
    if(0 != sem_init( &( (X) ), 1, (V) )) { \
        return FAILURE; \
    } \


TelemetryShm::TelemetryShm() {
    packet_blocks = NULL;
    info_blocks = NULL;
    master_block = NULL;
    num_packets = 0;
    last_nonces = NULL;
    last_nonce = 0;
    read_mode = STANDARD_READ;
}

TelemetryShm::~TelemetryShm() {
    if(packet_blocks) {
        for(size_t i = 0; i < num_packets; i++) {
            delete packet_blocks[i];
        }
    }

    if(info_blocks) {
        for(size_t i = 0; i < num_packets; i++) {
            delete info_blocks[i];
        }
    }

    if(master_block) {
        delete master_block;
    }

    if(last_nonces) {
        free(last_nonces);
    }
}

RetType TelemetryShm::init(VCM vcm) {

    // creates and zero last_nonces, TODO after num_packets is set
    last_nonces = (uint32_t*)malloc(num_packets * sizeof(uint32_t));
    memset(last_nonces, 0, num_packets * sizeof(uint32_t));

    // TODO setup shm block objects in list
    // need to create a list of Shm object pointers stored in packet_blocks
    // should be num_packets many blocks of the correct size according to the vcm file
    return FAILURE;
}

RetType TelemetryShm::init() {
    VCM vcm; // leave this on stack to be discarded after return
    return init(vcm);
}

RetType TelemetryShm::open() {
    for(size_t i = 0; i < num_packets; i++) {
        if(SUCCESS != packet_blocks[i]->attach()) {
            return FAILURE;
        }
        else if(SUCCESS != info_blocks[i]->attach()) {
            return FAILURE;
        }
    }

    if(SUCCESS != master_block->attach()) {
        return FAILURE;
    }

    return SUCCESS;
}

RetType TelemetryShm::close() {
    for(size_t i = 0; i < num_packets; i++) {
        if(SUCCESS != packet_blocks[i]->detach()) {
            return FAILURE;
        }
        else if(SUCCESS != info_blocks[i]->detach()) {
            return FAILURE;
        }
    }

    if(SUCCESS != master_block->detach()) {
        return FAILURE;
    }

    return SUCCESS;
}

// NOTE: does not attach!
RetType TelemetryShm::create() {
    for(size_t i = 0; i < num_packets; i++) {
        if(SUCCESS != packet_blocks[i]->create()) {
            return FAILURE;
        }
        else if(SUCCESS != info_blocks[i]->create()) {
            return FAILURE;
        }

        shm_info_t* info = (shm_info_t*)info_blocks[i]->data;

        // initialize info block
        // init semaphores
        INIT(info->rmutex, 1);
        INIT(info->wmutex, 1);
        INIT(info->readTry, 1);
        INIT(info->resource, 1);

        // init reader/writer counts to 0
        info->readers = 0;
        info->writers = 0;

        // start the nonce at 0
        info->nonce = 0;
    }

    if(SUCCESS != master_block->create()) {
        return FAILURE;
    }

    shm_info_t* info = (shm_info_t*)master_block->data;

    // init semaphores
    INIT(info->rmutex, 1);
    INIT(info->wmutex, 1);
    INIT(info->readTry, 1);
    INIT(info->resource, 1);

    // init reader/writer counts to 0
    info->readers = 0;
    info->writers = 0;

    // start the master nonce at 0
    info->nonce = 0;

    return SUCCESS;
}

// NOTE: must be attached already!
RetType TelemetryShm::destroy() {
    for(size_t i = 0; i < num_packets; i++) {
        if(SUCCESS != packet_blocks[i]->destroy()) {
            return FAILURE;
        }
        else if(SUCCESS != info_blocks[i]->destroy()) {
            return FAILURE;
        }
    }

    if(SUCCESS != master_block->destroy()) {
        return FAILURE;
    }

    return SUCCESS;
}

RetType TelemetryShm::write(unsigned int packet_id, uint8_t* data) {
    if(packet_id > num_packets) {
        // TODO sys message
        return FAILURE;
    }

    if(packet_blocks == NULL || info_blocks == NULL || master_block == NULL) {
        // not open
        // TODO sys message
        return FAILURE;
    }

    // packet_id is an index
    Shm* packet = packet_blocks[packet_id];
    uint32_t* packet_nonce = (uint32_t*)info_blocks[packet_id]->data;
    shm_info_t* info = (shm_info_t*)master_block->data;

    if(packet == NULL || info == NULL || packet_nonce == NULL) {
        // TODO sys message
        // something isn't attached
        return FAILURE;
    }

    // enter as a writer
    P(info->wmutex);
    info->writers++;
    if(info->writers == 1) {
        P(info->readTry);
    }
    V(info->wmutex);

    P(info->resource);

    memcpy((unsigned char*)packet->data, data, packet->size);
    info->nonce++; // update the master nonce

    (*packet_nonce)++; // update the packet nonce

    // wakeup anyone blocked on this packet (or any packet with an equivalen id mod 32)
    syscall(SYS_futex, &(info->nonce), FUTEX_WAKE_BITSET, INT_MAX, NULL, NULL, 1 << (packet_id % 32)); // TODO check return

    V(info->resource);

    P(info->wmutex);
    info->writers--;
    if(info->writers == 0) {
        V(info->readTry);
    }
    V(info->wmutex);

    return SUCCESS;
}

RetType TelemetryShm::clear(unsigned int packet_id, uint8_t val) {
    if(packet_id > num_packets) {
        // TODO sys message
        return FAILURE;
    }

    if(packet_blocks == NULL || info_blocks == NULL || master_block == NULL) {
        // not open
        // TODO sys message
        return FAILURE;
    }

    // packet_id is an index
    Shm* packet = packet_blocks[packet_id];
    uint32_t* packet_nonce = (uint32_t*)info_blocks[packet_id]->data;
    shm_info_t* info = (shm_info_t*)master_block->data;

    if(packet == NULL || info == NULL || packet_nonce) {
        // TODO sys message
        // something isn't attached
        return FAILURE;
    }

    // enter as a writer
    P(info->wmutex);
    info->writers++;
    if(info->writers == 1) {
        P(info->readTry);
    }
    V(info->wmutex);

    P(info->resource);

    memset((unsigned char*)packet->data, val, packet->size);
    info->nonce++; // update the master nonce

    (*packet_nonce)++; // update the packet nonce

    // wakeup anyone blocked on this packet
    // technically we can only block on up to 32 packets, but we mod the packet id so that
    // some packets may have to share, the reader should check to see if their packet really updated
    syscall(SYS_futex, info->nonce, FUTEX_WAKE_BITSET, INT_MAX, NULL, NULL, 1 << (packet_id % 32));

    V(info->resource);

    P(info->wmutex);
    info->writers--;
    if(info->writers == 0) {
        V(info->readTry);
    }
    V(info->wmutex);

    return SUCCESS;
}

RetType TelemetryShm::read_lock(unsigned int* packet_ids, size_t num) {
    if(info_blocks == NULL || master_block == NULL) {
        // not open
        // TODO sys message
        return FAILURE;
    }

    shm_info_t* info = (shm_info_t*)master_block->data;

    if(info == NULL) {
        // TODO sys message
        // something isn't attached
        return FAILURE;
    }

    // if we block, keep looping since we can't guarantee just because we were awoken our packet changed
    // this is due to having only 32 bits in the bitset but arbitrarily many packets
    // TODO in the future this loop could be avoided by setting a hard limit of 32 on the number of packets
    // then just immediately return after the futex syscall returns
    // otherwise we return at some point
    while(1) {
        // enter as a reader
        P(info->readTry);
        P(info->rmutex);
        info->readers++;
        if(info->readers == 1) {
            P(info->resource);
        }
        V(info->rmutex);
        V(info->readTry);

        // if reading in standard mode we never block so don't check
        if(read_mode == STANDARD_READ) {
            return SUCCESS;
        }

        // update the stored master nonce
        last_nonce = info->nonce;

        // check to see if any nonce has changed for the packets we're locking
        // if any nonce has changed we don't need to block
        uint32_t bitset = 0;
        uint32_t* nonce;
        unsigned int id;
        bool block = true; // whether or not to block
        for(size_t i = 0; i < num; i++) {
            bitset |= (1 << i);

            id = packet_ids[i];

            nonce = (uint32_t*)(info_blocks[id]->data);
            if(*nonce != last_nonces[id]) {
                // we found a nonce that changed!
                // important to not just return here since we need to update stored nonces
                last_nonces[id] = *nonce;
                block = false;
            }
        }

        // we found a changed nonce so we can return
        if(!block) {
            return SUCCESS;
        }

        // if we made it here none of our nonces changed :(
        // time to block
        read_unlock();

        if(read_mode == BLOCKING_READ) {
            syscall(SYS_futex, info->nonce, FUTEX_WAIT_BITSET, last_nonce, NULL, NULL, bitset);
        } else { // NONBLOCKING_READ
            return BLOCKED;
        }
    }
}

RetType TelemetryShm::read_lock() {
    if(info_blocks == NULL || master_block == NULL) {
        // not open
        // TODO sys message
        return FAILURE;
    }

    shm_info_t* info = (shm_info_t*)master_block->data;

    if(info == NULL) {
        // TODO sys message
        // something isn't attached
        return FAILURE;
    }

    // enter as a reader
    P(info->readTry);
    P(info->rmutex);
    info->readers++;
    if(info->readers == 1) {
        P(info->resource);
    }
    V(info->rmutex);
    V(info->readTry);

    // if reading in standard mode we never block so don't check
    if(read_mode == STANDARD_READ) {
        // update the master nonce and leave
        last_nonce = info->nonce;
        return SUCCESS;
    }

    if(last_nonce == info->nonce) { // nothing changed, block
        if(read_mode == BLOCKING_READ) {
            // wait for any packet to be updated
            // we don't need to loop here and check if it was our packet that updated since we don't care which packet updated
            syscall(SYS_futex, info->nonce, FUTEX_WAIT_BITSET, last_nonce, NULL, NULL, 0xFF);
        } else {
            return BLOCKED;
        }
    }

    return SUCCESS;
}

RetType TelemetryShm::read_unlock() {
    if(info_blocks == NULL || master_block == NULL) {
        // not open
        // TODO sys message
        return FAILURE;
    }

    // packet_id is an index
    shm_info_t* info = (shm_info_t*)master_block->data;

    if(info == NULL) {
        // TODO sys message
        // something isn't attached
        return FAILURE;
    }

    // exit as a reader
    P(info->rmutex);
    info->readers--;
    if(info->readers == 0) {
        V(info->resource);
    }
    V(info->rmutex);

    return SUCCESS;
}

#undef P
#undef V
#undef INIT
