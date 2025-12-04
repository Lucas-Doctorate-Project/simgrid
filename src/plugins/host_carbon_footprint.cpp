/* Copyright (c) 2010-2020. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "simgrid/Exception.hpp"
#include "simgrid/plugins/carbon_footprint.h"
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


SIMGRID_REGISTER_PLUGIN(host_carbon_footprint, "Host carbon footprint", &sg_host_carbon_footprint_plugin_init)


/** @addtogroup plugin_environmental_footprint

This is the environmental footprint plugin, enabling the simulation of CO₂ emissions and water consumption associated with the energy consumption of hosts in the simulated platform.
It calculates the total CO₂ emissions of each host based on its energy consumption and the CO₂ emission rate, and the total water consumption of each host based 
on its energy consumption and the water consumption rate.

To activate this plugin, first call :cpp:func:`sg_host_carbon_footprint_plugin_init()` before loading your platform. 
Then, use :cpp:func:`sg_host_get_carbon_footprint()` to retrieve the total CO₂ emissions of a given host, or :cpp:func:`sg_host_get_water_footprint()` 
to retrieve the total water consumption.

The carbon and water footprint are calculated dynamically during the simulation, taking into account the energy consumed by the host over time, the composition of the 
energy mix of the host and the carbon/water intensity of each energy source in the mix.
The energy mix is defined as a map, where each entry is keyed by the name of the energy source (like coal, natural gas, or renewables), and contains the percentage 
that each source contributes to the overall energy mix, along with its carbon intensity (in grams of CO₂ per kWh) and water intensity (in liters per kWh).
The energy mix can be configured for each host using :cpp:func:`sg_host_set_energy_mix()`.

As a result, the carbon footprint model requires the following parameters:
  - **Energy consumption**: The energy consumed by the host, which is calculated by the energy plugin based on the host's power profile and activity.
  - **Energy mix**: The composition of the energy sources used to generate the electricity consumed by the host (like coal composes 70%, hydro 23%, ...).
  - **Carbon footprint**: The amount of CO₂ consumed (in kilograms per kWh) to generate a certain amount of energy (1 kWh).
  - **Water footprint**: The amount of water consumed (in liters per kWh) to generate a certain amount of energy (1 kWh).

Here is an example of XML declaration:

.. code-block:: xml

   <host id="Host1" speed="100.0Mf, 1e-9Mf, 0.5f, 0.05f" pstate="0">
       <prop id="wattage_per_state" value="30.0:30.0:100.0, 9.75:9.75:9.75, 200.996721311:200.996721311:200.996721311, 425.1743849:425.1743849:425.1743849" />
       <prop id="wattage_off" value="9.75" />
        <prop id="energy_mix" value="Coal: 70;Hydro: 20;Solar: 10" />
        <prop id="carbon_footprint" value="Coal: 1000;Hydro: 24;Solar: 50" />
        <prop id="water_footprint" value="Coal: 1500;Hydro: 100;Solar: 50" />
   </host>

In this example:
- The `wattage_per_state` property defines the power consumption (in watts) for each pstate of the host.
- The `wattage_off` property defines the power consumption when the host is turned off.
- The `energy_mix` property defines the composition of the energy sources used to power the host.
- The `carbon_footprint` property defines the CO₂ emission rate (in grams per kWh) for the host.
- The `water_footprint` property defines the water consumption rate (in liters per kWh) for the host.


### How accurate are these models?
This model is still a work in progress and may not fully reflect real-world CO₂ emissions. The accuracy of the results depends on the quality
of the input parameters, such as the energy profile of the host and the CO₂ emission rate (carbon_intensity). Further improvements and evaluations of the model are needed.
Keep this in mind when using this plugin.

*/


XBT_LOG_NEW_DEFAULT_SUBCATEGORY(host_carbon_footprint, kernel, "Logging specific to the host environmental footprint plugin");


namespace simgrid::plugin {

class HostEnvironmentalFootprint {
  
public:
  static simgrid::xbt::Extension<simgrid::s4u::Host, HostEnvironmentalFootprint> EXTENSION_ID;

  explicit HostEnvironmentalFootprint(simgrid::s4u::Host *ptr);
  ~HostEnvironmentalFootprint();

  double get_host_carbon_footprint();
  double get_host_water_footprint();
  double get_last_update_time() const { return last_updated; }
  void set_host_energy_mix(const std::map<std::string, EnergySource>& mix);
  std::string get_host_energy_mix_formatted();
  void update();
private:
  simgrid::s4u::Host* host_ = nullptr;

  double last_updated = simgrid::s4u::Engine::get_clock(); /*< Timestamp of the last update event*/
  double last_energy;  /*< Amount of energy used so far (kwh) >*/
  
  std::map<std::string, EnergySource> energy_mix; /*< Energy sources making up the carbon emission mix */
  const std::map<std::string, EnergySource> default_energy_mix = {{"NULL_SOURCE", EnergySource{100.0, 0.0, 0.0}}}; /*< Default energy mix if none is provided */
  double total_carbon_footprint = 0.0; /* Total CO2 emitted to produce the energy used by the host */ 
  double total_water_footprint = 0.0; /* Total water used to produce the energy used by the host */

  std::map<std::string, double> parse_string_into_map(const char* input_cstr);
  void build_energy_mix(const std::map<std::string, double>& energy_percentage_map, 
                        const std::map<std::string, double>& carbon_footprint_map,
                        const std::map<std::string, double>& water_footprint_map);
  void validate_energy_mix_composition();
};

simgrid::xbt::Extension<simgrid::s4u::Host, HostEnvironmentalFootprint> HostEnvironmentalFootprint::EXTENSION_ID;

void HostEnvironmentalFootprint::update() 
{
  double start_time = this->last_updated;
  double finish_time = simgrid::s4u::Engine::get_clock();

  if (start_time >= finish_time) {
    return;
  }

  double instantaneous_power_consumption = sg_host_get_current_consumption(host_);
  
  double energy_this_step = instantaneous_power_consumption * (finish_time - start_time);
  double energy_this_step_kwh = energy_this_step / 3.6e6;

  double carbon_this_step = 0, water_this_step = 0;
  for (const auto& [source_name, source_info] : this->energy_mix) {
    carbon_this_step += ((source_info.carbon_intensity * source_info.percentage) * energy_this_step_kwh) / 100.0;
    water_this_step += ((source_info.water_intensity * source_info.percentage) * energy_this_step_kwh) / 100.0; 
  }
  
  double previous_carbon_footprint = this->total_carbon_footprint;
  double previous_water_footprint = this->total_water_footprint;
  this->total_carbon_footprint = previous_carbon_footprint + carbon_this_step;
  this->total_water_footprint = previous_water_footprint + water_this_step;

  this->last_updated = finish_time;

  XBT_DEBUG("[update_carbon_footprint of %s] period=[%.8f-%.8f]; instantaneous power=%.2f W;"
            "total carbon footprint before: %.8f g -> added now: %.8f g;"
            "total water footprint before: %.8f L -> added now: %.8f g",
            host_->get_cname(), start_time, finish_time, instantaneous_power_consumption, 
            previous_carbon_footprint, carbon_this_step, previous_water_footprint, water_this_step);
}

HostEnvironmentalFootprint::HostEnvironmentalFootprint(simgrid::s4u::Host* ptr) : host_(ptr) 
{
  this->last_energy = sg_host_get_consumed_energy(host_);

  const char* raw_energy_mix = host_->get_property("energy_mix");
  const char* raw_carbon_footprint = host_->get_property("carbon_footprint");
  const char* raw_water_footprint = host_->get_property("water_footprint");

  if (raw_carbon_footprint == nullptr && raw_water_footprint == nullptr) {
    XBT_WARN("Host '%s': Missing values for properties 'carbon_footprint' and 'water_footprint', using default null energy mix.", host_->get_cname());
    this->energy_mix = this->default_energy_mix; // Default to 0 g/kWh and 0 L/kWh if not provided
  } else {
    std::map<std::string, double> energy_percentage_map = this->parse_string_into_map(raw_energy_mix);
    std::map<std::string, double> carbon_footprint_map = this->parse_string_into_map(raw_carbon_footprint);
    std::map<std::string, double> water_footprint_map = this->parse_string_into_map(raw_water_footprint);

    this->build_energy_mix(energy_percentage_map, carbon_footprint_map, water_footprint_map);
    this->validate_energy_mix_composition();
  }

  XBT_DEBUG("Creating HostEnvironmentalFootprint for host %s with the following energy mix configuration: \n%s.", host_->get_cname(), this->get_host_energy_mix_formatted().c_str());
}

double HostEnvironmentalFootprint::get_host_carbon_footprint() 
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  return this->total_carbon_footprint;
}

double HostEnvironmentalFootprint::get_host_water_footprint() 
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  return this->total_water_footprint;
}

void HostEnvironmentalFootprint::set_host_energy_mix(const std::map<std::string, EnergySource>& mix)
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  this->energy_mix = mix;
}

std::string HostEnvironmentalFootprint::get_host_energy_mix_formatted() 
{
  std::stringstream ss;  
  
  ss << std::fixed << std::setprecision(2);

    for (const auto& entry : this->energy_mix) {
        const std::string& source_name = entry.first;
        double percentage = entry.second.percentage;
        double carbon_footprint = entry.second.carbon_intensity;
        double water_footprint = entry.second.water_intensity;

        ss << source_name << ": "<< carbon_footprint << " gCO2/kWh, " << water_footprint << " L/kWh, (" << percentage << "%)\n"; 
    }

    return ss.str();
}

HostEnvironmentalFootprint::~HostEnvironmentalFootprint() = default;

std::map<std::string, double> HostEnvironmentalFootprint::parse_string_into_map(const char* input_cstr) 
{
  std::map<std::string, double> result;
  if (input_cstr == nullptr) return result;

  std::string input(input_cstr);
  std::stringstream input_stream(input);
  std::string entry;

  while (std::getline(input_stream, entry, ';')) {
    std::stringstream entry_stream(entry);
    std::string key;
    std::string val_str;

    if (std::getline(entry_stream, key, ':') && std::getline(entry_stream, val_str)) {
      try {
        result[key] = std::stod(val_str);
      } catch (...) {
        XBT_WARN("Failed to parse value for '%s'.", key.c_str());
      }
    }
  }
  return result;
}

void HostEnvironmentalFootprint::build_energy_mix(const std::map<std::string, double>& energy_percentage_map,
                      const std::map<std::string, double>& carbon_footprint_map,
                      const std::map<std::string, double>& water_footprint_map) 
{
  for (const auto& [source_name, percentage] : energy_percentage_map) {
    double carbon_intensity = 0.0, water_intensity = 0.0;

    auto carbon_it = carbon_footprint_map.find(source_name);
    if (carbon_it != carbon_footprint_map.end()) {
      carbon_intensity = carbon_it->second;
    } else {
      XBT_WARN("Warning: Missing carbon intensity for source '%s' on host '%s'. Using 0 g/kWh.", source_name.c_str(), host_->get_cname());
    }

    auto water_it = water_footprint_map.find(source_name);
    if (water_it != water_footprint_map.end()) {
      water_intensity = water_it->second;
    } else {
      XBT_WARN("Warning: Missing water intensity for source '%s' on host '%s'. Using 0 L/kWh.", source_name.c_str(), host_->get_cname());
    }

    EnergySource source_info;
    source_info.percentage = percentage;
    source_info.carbon_intensity = carbon_intensity;
    source_info.water_intensity = water_intensity;

    this->energy_mix[source_name] = source_info;
  }
}

void HostEnvironmentalFootprint::validate_energy_mix_composition()
{
  double total = 0.0;
  for (const auto& [source_name, source_info] : this->energy_mix) {
    total += source_info.percentage;
  }

  if (total > 0 && !double_equals(total, 100.0, 1E-9)) {
      XBT_WARN("Host '%s' eco mix sums to %.2f%%, not 100%%. Using default emission mix.", host_->get_cname(), total);
      this->energy_mix = this->default_energy_mix;
  }
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

      // Get the host_carbon_footprint extension for the relevant host
      auto* host_carbon_footprint = host->extension<HostEnvironmentalFootprint>();

      if (host_carbon_footprint->get_last_update_time() < simgrid::s4u::Engine::get_clock())
        host_carbon_footprint->update();
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

  XBT_INFO("Carbon emitted by host %s: %f g", host.get_cname(),
           host.extension<HostEnvironmentalFootprint>()->get_host_carbon_footprint());
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
void sg_host_carbon_footprint_plugin_init()
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
    throw simgrid::xbt::InitializationError("The Carbon Footprint plugin is not active. Please call sg_host_carbon_footprint_plugin_init() "
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

void sg_host_set_energy_mix(const_sg_host_t host, const std::map<std::string, EnergySource>& mix)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_host_energy_mix(mix);
}

std::string sg_host_get_energy_mix_formatted(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_host_energy_mix_formatted();
}