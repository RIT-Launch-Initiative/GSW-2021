/********************************************************************
*  Name: calc.h
*
*  Purpose: Functions to handle calculating virtual telemetry values.
*
*  Author: Will Merges
*
*  RIT Launch Initiative
*********************************************************************/
#ifndef CALC_H
#define CALC_H

#include "common/types.h"
#include "lib/vcm/vcm.h"
#include <stdint.h>
#include <string>
#include <vector>

using namespace vcm;

namespace calc {

    typedef struct {
        measurement_info_t* meas;
        uint8_t* addr;
    } arg_t;

    typedef struct {
        size_t unique_id;
        measurement_info_t* out;
        RetType (*convert_function)(arg_t& out, std::vector<arg_t>& args);
        std::vector<measurement_info_t*> args;
    } vcalc_t;

    // parse the default file into 'entries'
    RetType parse_vfile(VCM* veh, std::vector<vcalc_t>* entries);
}


#endif
