/* Copyright (c) 2010-2020. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "simgrid/Exception.hpp"
#include "simgrid/plugins/environmental_footprint.h"
#include "simgrid/s4u/Engine.hpp"
#include "simgrid/plugins/energy.h"
#include "simgrid/s4u/VirtualMachine.hpp"
#include <simgrid/s4u/Host.hpp>
#include "simgrid/simcall.hpp"
#include "src/simgrid/module.hpp"

#include "src/kernel/resource/CpuImpl.hpp"
#include "src/kernel/activity/ActivityImpl.hpp"


#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <iomanip>
#include <src/simgrid/math_utils.h>


SIMGRID_REGISTER_PLUGIN(host_environmental_footprint, "Host environmental footprint", &sg_host_environmental_footprint_plugin_init)


/** @addtogroup plugin_environmental_footprint

This is the environmental footprint plugin. It calculates both carbon and water footprint
and intensity factors of SimGrid hosts.

To calculate the intensities, we average known intensity factors from different sources
according to the energy mix distribution. If we have \f$N\f$ distinct energy sources,
the weighted intensity \f$\bar{I}\f$ can be calculated by:

\f[
  \bar{I} = \frac{\sum_{j=1}^{N} (I_j \cdot w_j)}{\sum_{j=1}^{N} w_j}
\f]

where \f$I_j\f$ is the intensity factor of source \f$j\f$, and \f$w_j\f$ is the proportion
of the contribution of source \f$j\f$ to the energy mix. Note that \f$\sum_{j=1}^{N} w_j = 1\f$.

From these averaged intensities, we can calculate the footprint \f$F\f$ by knowing how much
energy a specific host consumes (\f$E\f$):

\f[
  F = \bar{I} \cdot E
\f]

The plugin calculates:
- The total CO₂ emissions of each host based on its energy consumption and the CO₂ emission rate
- The total water consumption of each host based on its energy consumption and the water consumption rate

To activate this plugin, first call :cpp:func:`sg_host_environmental_footprint_plugin_init()` before loading your platform.
Then, use :cpp:func:`sg_host_get_carbon_footprint()` to retrieve the total CO₂ emissions of a given host, or :cpp:func:`sg_host_get_water_footprint()`
to retrieve the total water consumption.

The footprints are calculated by multiplying the energy consumed by the host (tracked by the energy plugin) with the
weighted intensity factors derived from the configured energy mix. Updates occur automatically when the host changes
state, changes speed, or when footprint values are queried. The total footprint accumulates incrementally over the
simulation time.

The energy mix is defined as a map where each entry is keyed by the name of the energy source (e.g., coal, natural gas, renewables).
Each entry specifies the percentage that source contributes to the overall energy mix, along with its carbon intensity
(in grams of CO₂ per kWh) and water intensity (in liters per kWh). The energy mix can be configured for each host using
:cpp:func:`sg_host_set_energy_mix()`.

The model requires the following parameters:
  - **Energy consumption**: Calculated by the energy plugin based on the host's power profile and activity
  - **Energy mix**: The composition of energy sources (e.g., coal 70%, hydro 20%, solar 10%)
  - **Carbon intensity**: The amount of CO₂ emitted (in grams per kWh) to generate energy from each source
  - **Water intensity**: The amount of water consumed (in liters per kWh) to generate energy from each source

Here is an example of XML declaration:

.. code-block:: xml

   <host id="Host1" speed="100.0Mf, 1e-9Mf, 0.5f, 0.05f" pstate="0">
       <prop id="wattage_per_state" value="30.0:30.0:100.0, 9.75:9.75:9.75, 200.996721311:200.996721311:200.996721311, 425.1743849:425.1743849:425.1743849" />
       <prop id="wattage_off" value="9.75" />
        <prop id="energy_mix" value="Coal:70;Hydro:20;Solar:10" />
        <prop id="carbon_intensity" value="Coal:1000;Hydro:24;Solar:50" />
        <prop id="water_intensity" value="Coal:1500;Hydro:100;Solar:50" />
   </host>


### How accurate are these models?
This model is still a work in progress and may not fully reflect real-world CO₂ emissions. The accuracy of the results depends on the quality
of the input parameters, such as the energy profile of the host and the CO₂ emission rate (carbon_intensity). Further improvements and evaluations of the model are needed.
Keep this in mind when using this plugin.

*/


XBT_LOG_NEW_DEFAULT_SUBCATEGORY(host_environmental_footprint, kernel, "Logging specific to the host environmental footprint plugin");


namespace simgrid::plugin {

class HostEnvironmentalFootprint {
  
public:
  static simgrid::xbt::Extension<simgrid::s4u::Host, HostEnvironmentalFootprint> EXTENSION_ID;

  explicit HostEnvironmentalFootprint(simgrid::s4u::Host *ptr);
  ~HostEnvironmentalFootprint();

  double get_host_carbon_footprint();
  double get_host_water_footprint();
  double get_host_carbon_intensity();
  void set_carbon_intensity(double new_intensity);
  double get_host_water_intensity();
  void set_water_intensity(double new_intensity);
  double get_last_update_time() const { return last_updated_; }
  void set_pue(double new_pue);
  void set_wue(double new_wue);
  void update();
private:
  simgrid::s4u::Host* host_ = nullptr;

  double last_updated_ = simgrid::s4u::Engine::get_clock(); /*< Timestamp of the last update event*/
  
  double pue_ = 1.0;           // Power Usage Effectiveness (default: ideal)
  double wue_ = 0.0;           // Water Usage Effectiveness (L/kWh) - on-site cooling

  double embodied_carbon_ = 0.0;  // total gCO2 embodied in host production 
  double embodied_water_  = 0.0;  // total L embodied in host production
  double host_lifetime_seconds_  = 0.0;  // Expected useful life of the host (seconds)

  double total_carbon_footprint_ = 0.0; /* Total CO2 emitted to produce the energy used by the host */ 
  double total_water_footprint_ = 0.0; /* Total water used to produce the energy used by the host */
  
  double carbon_intensity_ = 0.0; /* Current weighted carbon intensity of the energy mix (g CO2/kWh) */
  double water_intensity_ = 0.0; /* Current weighted water intensity of the energy mix (L/kWh) */

  void ensure_up_to_date();
};

simgrid::xbt::Extension<simgrid::s4u::Host, HostEnvironmentalFootprint> HostEnvironmentalFootprint::EXTENSION_ID;

void HostEnvironmentalFootprint::update() 
{
  double start_time = this->last_updated_;
  double finish_time = simgrid::s4u::Engine::get_clock();

  if (start_time >= finish_time) {
    return;
  }
  double deltaT = finish_time - start_time;

  double instantaneous_power_consumption = sg_host_get_current_consumption(host_);
  
  double energy_this_step = instantaneous_power_consumption * deltaT;
  double computation_energy_this_step_kwh = energy_this_step / 3.6e6;
  double total_datacenter_energy_this_step_kwh = computation_energy_this_step_kwh * this->pue_;

  double lifetime_fraction_this_step = (this->host_lifetime_seconds_ > 0.0) ? (deltaT / this->host_lifetime_seconds_) : 0.0;

  double offsite_carbon_footprint = total_datacenter_energy_this_step_kwh * this->carbon_intensity_;
  double embodied_carbon_footprint = this->embodied_carbon_ * lifetime_fraction_this_step;
  
  double onsite_water_consumption = computation_energy_this_step_kwh * this->wue_;
  double offsite_water_consumption = total_datacenter_energy_this_step_kwh * this->water_intensity_;
  double embodied_water_consumption = this->embodied_water_ * lifetime_fraction_this_step;

  this->total_carbon_footprint_ = this->total_carbon_footprint_ + offsite_carbon_footprint + embodied_carbon_footprint;
  this->total_water_footprint_ = this->total_water_footprint_ + offsite_water_consumption + onsite_water_consumption + embodied_water_consumption;

  this->last_updated_ = finish_time;

  XBT_DEBUG("[update_carbon_footprint of %s] period=[%.8f-%.8f]; instantaneous power=%.2f W;"
            "total carbon footprint now: %.8f gCO2eq; total water footprint now: %.8f L",
            host_->get_cname(), start_time, finish_time, instantaneous_power_consumption, 
            this->total_carbon_footprint_, this->total_water_footprint_);
}

HostEnvironmentalFootprint::HostEnvironmentalFootprint(simgrid::s4u::Host* ptr) : host_(ptr) 
{
  const char* raw_carbon_intensity = host_->get_property("carbon_intensity");
  if (raw_carbon_intensity != nullptr)
    this->carbon_intensity_ = std::stod(raw_carbon_intensity);

  const char* raw_water_intensity = host_->get_property("water_intensity");
  if (raw_water_intensity != nullptr)
    this->water_intensity_ = std::stod(raw_water_intensity);
  
  const char* raw_pue = host_->get_property("pue");
  if (raw_pue != nullptr)
    this->pue_ = std::stod(raw_pue);

  const char* raw_wue = host_->get_property("wue");
  if (raw_wue != nullptr)
    this->wue_ = std::stod(raw_wue);

  const char* raw_emb_carbon = host_->get_property("embodied_carbon");
  if (raw_emb_carbon != nullptr)
    this->embodied_carbon_ = std::stod(raw_emb_carbon);

  const char* raw_emb_water = host_->get_property("embodied_water");
  if (raw_emb_water != nullptr)
    this->embodied_water_ = std::stod(raw_emb_water);

  const char* raw_lifetime = host_->get_property("host_lifetime");
  if (raw_lifetime != nullptr)
    this->host_lifetime_seconds_ = std::stod(raw_lifetime);

  XBT_DEBUG("Creating HostEnvironmentalFootprint for host %s with the following energy mix configuration: \nCarbon intensity: %.2f gCO2/kWh; Water intensity: %.2f L/kWh.",
            host_->get_cname(), this->carbon_intensity_, this->water_intensity_);
}

double HostEnvironmentalFootprint::get_host_carbon_footprint() 
{
  this->ensure_up_to_date();
  return this->total_carbon_footprint_;
}

double HostEnvironmentalFootprint::get_host_water_footprint() 
{
  this->ensure_up_to_date();
  return this->total_water_footprint_;
}

double HostEnvironmentalFootprint::get_host_carbon_intensity() 
{
  this->ensure_up_to_date();
  return this->carbon_intensity_;
}

void HostEnvironmentalFootprint::set_carbon_intensity(double new_intensity)
{
  this->ensure_up_to_date();
  this->carbon_intensity_ = new_intensity;
}

double HostEnvironmentalFootprint::get_host_water_intensity() 
{
  this->ensure_up_to_date();
  return this->water_intensity_;
}

void HostEnvironmentalFootprint::set_water_intensity(double new_intensity)
{
  this->ensure_up_to_date();
  this->water_intensity_ = new_intensity;
}

void HostEnvironmentalFootprint::set_wue(double new_wue)
{
  this->ensure_up_to_date();
  this->wue_ = new_wue;
}

void HostEnvironmentalFootprint::set_pue(double new_pue)
{
  this->ensure_up_to_date();
  this->pue_ = new_pue;
}

HostEnvironmentalFootprint::~HostEnvironmentalFootprint() = default;


void HostEnvironmentalFootprint::ensure_up_to_date() 
{
  if (this->last_updated_ < simgrid::s4u::Engine::get_clock())
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));
}


} // namespace simgrid::plugin

using simgrid::plugin::HostEnvironmentalFootprint;

/* **************************** events  callback *************************** */

static void on_creation(simgrid::s4u::Host& host)
{

  if (dynamic_cast<simgrid::s4u::VirtualMachine*>(&host)) // Ignore virtual machines
    return;

  if (!host.extension<HostEnvironmentalFootprint>()) {
    host.extension_set(new HostEnvironmentalFootprint(&host));
  } else {
    return;
  }

}

static void on_action_state_change(simgrid::kernel::resource::CpuAction const& action,
                                   simgrid::kernel::resource::Action::State /*previous*/)
{
 for (simgrid::kernel::resource::CpuImpl const* cpu : action.cpus()) {
    simgrid::s4u::Host* host = cpu->get_iface();
    if (host != nullptr) {
      // If it's a VM, take the corresponding PM
      if (const auto* vm = dynamic_cast<simgrid::s4u::VirtualMachine*>(host))
        host = vm->get_pm();

      // Get the host_environmental_footprint extension for the relevant host
      auto* host_environmental_footprint = host->extension<HostEnvironmentalFootprint>();

      if (host_environmental_footprint->get_last_update_time() < simgrid::s4u::Engine::get_clock())
        host_environmental_footprint->update();
    }
  }
}

/* This callback is fired either when the host changes its state (on/off) ("onStateChange") or its speed
 * (because the user changed the pstate, or because of external trace events) ("onSpeedChange") */
static void on_host_change(simgrid::s4u::Host const& h)
{
  const auto* host = &h;
  if (const auto* vm = dynamic_cast<simgrid::s4u::VirtualMachine const*>(host)) // Take the PM of virtual machines
    host = vm->get_pm();

  host->extension<HostEnvironmentalFootprint>()->update();
}

static void on_host_destruction(simgrid::s4u::Host const& host)
{
  if (dynamic_cast<simgrid::s4u::VirtualMachine const*>(&host)) // Ignore virtual machines
    return;

  XBT_INFO("Host %s: Carbon emitted by host: %f g. Water consumed by host: %f L.", host.get_cname(),
           host.extension<HostEnvironmentalFootprint>()->get_host_carbon_footprint(), host.extension<HostEnvironmentalFootprint>()->get_host_water_footprint());
}


static void on_simulation_end()
{
  //Do nothing. Maybe do something in another version.
  return;
}


/* **************************** Public interface *************************** */


/** \ingroup plugin_carbon_footprint
 * \brief Enable host carbon footprint plugin
 * \details Enable carbon footprint plugin to get the carbon footprint of each cpu. 
 */
void sg_host_environmental_footprint_plugin_init()
{
  if (HostEnvironmentalFootprint::EXTENSION_ID.valid())
    return;
  
  sg_host_energy_plugin_init();
  HostEnvironmentalFootprint::EXTENSION_ID = simgrid::s4u::Host::extension_create<HostEnvironmentalFootprint>();

  simgrid::s4u::Host::on_creation_cb(&on_creation);
  simgrid::s4u::Host::on_onoff_cb(&on_host_change);
  simgrid::s4u::Host::on_destruction_cb(&on_host_destruction);
  simgrid::s4u::Host::on_exec_state_change_cb(&on_action_state_change);
}


static void ensure_plugin_inited()
{
  if (not HostEnvironmentalFootprint::EXTENSION_ID.valid())
    throw simgrid::xbt::InitializationError("The Carbon Footprint plugin is not active. Please call sg_host_environmental_footprint_plugin_init() "
                                            "before calling any function related to that plugin.");
}

/** @ingroup plugin_carbon_footprint
 *  @brief Returns the total carbon footprint by the host so far (in g/kwh)
 *
 *  Please note that since the carbon footprint is lazily updated, it may require a simcall to update it.
 *  The result is that the actor requesting this value will be interrupted,
 *  the value will be updated in kernel mode before returning the control to the requesting actor.
 */

double sg_host_get_carbon_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_host_carbon_footprint();
}

double sg_host_get_water_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_host_water_footprint();
}

double sg_host_get_carbon_intensity(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_host_carbon_intensity();
}

void sg_host_set_carbon_intensity(const_sg_host_t host, double new_intensity)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_carbon_intensity(new_intensity);
}

double sg_host_get_water_intensity(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_host_water_intensity();
}

void sg_host_set_water_intensity(const_sg_host_t host, double new_intensity)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_water_intensity(new_intensity);
}

void sg_host_set_pue(const_sg_host_t host, double new_pue)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_pue(new_pue);
}

void sg_host_set_wue(const_sg_host_t host, double new_wue)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_wue(new_wue);
}
