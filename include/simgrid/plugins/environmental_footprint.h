/* Copyright (c) 2023-2024. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SIMGRID_PLUGINS_ENVIRONMENTAL_FOOTPRINT_H_
#define SIMGRID_PLUGINS_ENVIRONMENTAL_FOOTPRINT_H_

#include <simgrid/config.h>
#include <simgrid/forward.h>
#include <xbt/base.h>

#include <map>
#include <string>
#include <utility>

SG_BEGIN_DECL

struct EnergySource {
    double percentage = 0.0; // How much this source contributes to the mix (in %)
    double carbon_intensity = 0.0; // Grams of CO2 emitted per kWh produced
    double water_intensity = 0.0; // Liters of water consumed per kWh produced
};

XBT_PUBLIC void sg_host_environmental_footprint_plugin_init();
XBT_PUBLIC double sg_host_get_carbon_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_water_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_carbon_operational_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_carbon_embodied_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_water_onsite_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_water_offsite_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_water_embodied_footprint(const_sg_host_t host);
XBT_PUBLIC double sg_host_get_carbon_intensity(const_sg_host_t host);
XBT_PUBLIC void sg_host_set_carbon_intensity(const_sg_host_t host, double new_intensity);
XBT_PUBLIC double sg_host_get_water_intensity(const_sg_host_t host);
XBT_PUBLIC void sg_host_set_water_intensity(const_sg_host_t host, double new_intensity);
XBT_PUBLIC void sg_host_set_pue(const_sg_host_t host, double new_pue);
XBT_PUBLIC double sg_host_get_pue(const_sg_host_t host);
XBT_PUBLIC void sg_host_set_wue(const_sg_host_t host, double new_wue);
XBT_PUBLIC double sg_host_get_wue(const_sg_host_t host);

SG_END_DECL

#endif