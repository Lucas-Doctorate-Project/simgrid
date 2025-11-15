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


SIMGRID_REGISTER_PLUGIN(host_carbon_footprint, "Host carbon footprint", &sg_host_carbon_footprint_plugin_init)


/** @addtogroup plugin_carbon_footprint

This is the carbon footprint plugin, enabling the simulation of CO₂ emissions associated with the energy consumption of hosts in the simulated platform.
It calculates the total CO₂ emissions of each host based on its energy consumption and the CO₂ emission rate.
To activate this plugin, first call :cpp:func:`sg_host_carbon_footprint_plugin_init()` before loading your platform. Then, use :cpp:func:`sg_host_get_carbon_footprint()` 
to retrieve the total CO₂ emissions of a given host.

The carbon footprint is calculated dynamically during the simulation, taking into account the energy consumed by the host over time and the carbon
emission grid. 
The carbon emission mix is defined as a vector, where each entry contains the carbon consumed to generate energy for a specific source (like coal, natural gas, or renewables), 
in kilograms of CO₂ per kWh, and the percentage that each source contributes to the overall energy mix.
The emission rate can be configured for each host using :cpp:func:`sg_host_set_carbon_intensity()`.

As a result, the carbon footprint model requires the following parameters:

  - **Energy consumption**: The energy consumed by the host, which is calculated by the energy plugin based on the host's power profile and activity.
  - **Carbon emission mix**: The amount of CO₂ consumed (in kilograms per kWh) of energy generated, and the percentage that each source contributes to the overall energy mix.

Here is an example of XML declaration:

.. code-block:: xml

   <host id="Host1" speed="100.0Mf, 1e-9Mf, 0.5f, 0.05f" pstate="0">
       <prop id="wattage_per_state" value="30.0:30.0:100.0, 9.75:9.75:9.75, 200.996721311:200.996721311:200.996721311, 425.1743849:425.1743849:425.1743849" />
       <prop id="wattage_off" value="9.75" />
       <prop id="carbon_intensity" value="30" />
   </host>

In this example:
- The `wattage_per_state` property defines the power consumption (in watts) for each pstate of the host.
- The `wattage_off` property defines the power consumption when the host is turned off.
- The `carbon_intensity` property defines the CO₂ emission rate (in grams per kWh) for the host.


### How accurate are these models?
This model is still a work in progress and may not fully reflect real-world CO₂ emissions. The accuracy of the results depends on the quality
of the input parameters, such as the energy profile of the host and the CO₂ emission rate (carbon_intensity). Further improvements and evaluations of the model are needed.
Keep this in mind when using this plugin.

*/


XBT_LOG_NEW_DEFAULT_SUBCATEGORY(host_carbon_footprint, kernel, "Logging specific to the host carbon footprint plugin");


namespace simgrid::plugin {

class HostCarbonFootprint {
  
public:
  static simgrid::xbt::Extension<simgrid::s4u::Host, HostCarbonFootprint> EXTENSION_ID;

  double last_energy;  /*< Amount of energy used so far (kwh) >*/

  explicit HostCarbonFootprint(simgrid::s4u::Host *ptr);
  ~HostCarbonFootprint(); 

  double getHostCarbonFootprint();
  double get_last_update_time() const { return last_updated; }
  void setHostCarbonEmissionMix(const std::map<std::string, std::pair<double, double>>& mix);
  std::string getHostCarbonEmissionMixFormatted();
  void update();

private:
  simgrid::s4u::Host* host_ = nullptr;

  double total_carbon_footprint = 0.0; /*< Total CO2 emitted by the host */ 
  std::map<std::string, std::pair<double, double>> carbon_emission_mix; /*< Carbon emission mix of the host (source -> (kgCO2/kWh, percentage)) */
  const std::map<std::string, std::pair<double, double>> default_carbon_emission_mix = {{"Placeholder", std::make_pair(0.0, 100.0)}}; /*< Default carbon emission mix if none is provided */
  double last_updated = simgrid::s4u::Engine::get_clock(); /*< Timestamp of the last update event*/


  std::map<std::string, std::pair<double, double>> parse_carbon_mix(const std::string& carbon_mix_input) {
    std::map<std::string, std::pair<double, double>> carbon_emission_map;

    std::stringstream input_stream(carbon_mix_input);
    std::string entry;

    double total_percentage = 0.0;
    while (std::getline(input_stream, entry, ';')) {
        std::stringstream entry_stream(entry);
        std::string source_name, values_section;

        if (std::getline(entry_stream, source_name, ':') && std::getline(entry_stream, values_section)) {
            std::stringstream values_stream(values_section);
            double emission = 0.0;
            double percentage = 0.0;

            if (values_stream >> emission >> percentage) {
                carbon_emission_map[source_name] = std::make_pair(emission, percentage);
                total_percentage += percentage;
            } else {
                XBT_WARN("Warning: Malformed values for %s: '%s'. Using default emission mix.", source_name.c_str(), values_section.c_str());
                return this->default_carbon_emission_mix;
            }

        } else {
            XBT_WARN("Warning: Malformed entry: '%s'. Using default emission mix.", entry.c_str());
            return this->default_carbon_emission_mix;
        }
    }

    if (total_percentage != 100.0) {
        XBT_WARN("Warning: Total percentage of carbon mix is %.2f, expected 100.0. Using default emission mix.", total_percentage);
        return this->default_carbon_emission_mix;
    }

    return carbon_emission_map;
  }

};

simgrid::xbt::Extension<simgrid::s4u::Host, HostCarbonFootprint> HostCarbonFootprint::EXTENSION_ID;

void HostCarbonFootprint::update() {
  double start_time = this->last_updated;
  double finish_time = simgrid::s4u::Engine::get_clock();

  if (start_time >= finish_time) {
    return;
  }

  double instantaneous_power_consumption = sg_host_get_current_consumption(host_);
  
  double energy_this_step = instantaneous_power_consumption * (finish_time - start_time);
  double energy_this_step_kwh = energy_this_step / 3.6e6;

  double carbon_this_step = 0;
  for (const auto& [source, values] : this->carbon_emission_mix) {
    double source_kgCO2_per_kWh = values.first;
    double source_percentage = values.second;
    carbon_this_step += (source_kgCO2_per_kWh * source_percentage) * energy_this_step_kwh * 1000.0; 
  }
  
  double previous_carbon_footprint = this->total_carbon_footprint;
  this->total_carbon_footprint = previous_carbon_footprint + carbon_this_step;
  this->last_updated = finish_time;

  XBT_DEBUG("[update_carbon_footprint of %s] period=[%.8f-%.8f]; instantaneous power=%.2f W;"
            "total carbon footprint before: %.8f g -> added now: %.8f g",
            host_->get_cname(), start_time, finish_time, instantaneous_power_consumption, 
            previous_carbon_footprint, carbon_this_step);
}

HostCarbonFootprint::HostCarbonFootprint(simgrid::s4u::Host* ptr) : host_(ptr) {
  this->last_energy = sg_host_get_consumed_energy(host_);

  const char* raw_carbon_mix = host_->get_property("carbon_emission_mix");
  if (raw_carbon_mix != nullptr) {
    std::string carbon_mix_str(raw_carbon_mix);
    this->carbon_emission_mix = parse_carbon_mix(carbon_mix_str);
  } else {
    XBT_WARN("Host '%s': Missing value for property 'carbon_emission_mix', using default map.", host_->get_cname());
    this->carbon_emission_mix = this->default_carbon_emission_mix; // Default to 0 g/kWh if not provided
  }

  XBT_DEBUG("Creating HostCarbonFootprint for host %s with the following carbon emission mix configuration: \n%s.", host_->get_cname(), this->getHostCarbonEmissionMixFormatted().c_str());
}


double HostCarbonFootprint::getHostCarbonFootprint() {
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostCarbonFootprint::update, this));

  return this->total_carbon_footprint;
}

void HostCarbonFootprint::setHostCarbonEmissionMix(const std::map<std::string, std::pair<double, double>>& mix)
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostCarbonFootprint::update, this));

  this->carbon_emission_mix = mix;
}

std::string HostCarbonFootprint::getHostCarbonEmissionMixFormatted() {
  std::stringstream ss;  
  
  ss << std::fixed << std::setprecision(2);

    for (const auto& entry : this->carbon_emission_mix) {
        const std::string& source_name = entry.first;
        double emission                 = entry.second.first;
        double percentage               = entry.second.second;

        ss << source_name << ": "<< emission << " kgCO2/kWh (" << percentage << "%)" << "\n"; 
    }

    return ss.str();
}

HostCarbonFootprint::~HostCarbonFootprint() = default;


} // namespace simgrid::plugin

using simgrid::plugin::HostCarbonFootprint;

/* **************************** events  callback *************************** */

static void on_creation(simgrid::s4u::Host& host)
{

  if (dynamic_cast<simgrid::s4u::VirtualMachine*>(&host)) // Ignore virtual machines
    return;

  if (!host.extension<HostCarbonFootprint>()) {
    host.extension_set(new HostCarbonFootprint(&host));
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
      auto* host_carbon_footprint = host->extension<HostCarbonFootprint>();

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

  host->extension<HostCarbonFootprint>()->update();
}

static void on_host_destruction(simgrid::s4u::Host const& host)
{
  if (dynamic_cast<simgrid::s4u::VirtualMachine const*>(&host)) // Ignore virtual machines
    return;

  XBT_INFO("Carbon emitted by host %s: %f g", host.get_cname(),
           host.extension<HostCarbonFootprint>()->getHostCarbonFootprint());
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
  if (HostCarbonFootprint::EXTENSION_ID.valid())
    return;
  
  sg_host_energy_plugin_init();
  HostCarbonFootprint::EXTENSION_ID = simgrid::s4u::Host::extension_create<HostCarbonFootprint>();

  simgrid::s4u::Host::on_creation_cb(&on_creation);
  simgrid::s4u::Host::on_onoff_cb(&on_host_change);
  simgrid::s4u::Host::on_destruction_cb(&on_host_destruction);
  simgrid::s4u::Host::on_exec_state_change_cb(&on_action_state_change);
}


static void ensure_plugin_inited()
{
  if (not HostCarbonFootprint::EXTENSION_ID.valid())
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
  return host->extension<HostCarbonFootprint>()->getHostCarbonFootprint();
}
