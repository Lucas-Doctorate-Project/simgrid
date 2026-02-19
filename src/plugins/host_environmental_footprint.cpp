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
  double get_host_water_intensity();
  double get_last_update_time() const { return last_updated; }
  void set_host_energy_mix(const std::map<std::string, EnergySource>& mix);
  std::string get_host_energy_mix_formatted();
  void set_host_energy_mix_composition(const std::map<std::string, double>& composition);
  void set_carbon_intensities(const std::map<std::string, double>& intensities);
  void set_water_intensities(const std::map<std::string, double>& intensities);
  void update();
private:
  simgrid::s4u::Host* host_ = nullptr;

  double last_updated = simgrid::s4u::Engine::get_clock(); /*< Timestamp of the last update event*/
  double last_energy;  /*< Amount of energy used so far (kwh) >*/
  
  std::map<std::string, EnergySource> energy_mix; /*< Energy sources making up the carbon emission mix */
  const std::map<std::string, EnergySource> default_energy_mix = {{"NULL_SOURCE", EnergySource{100.0, 0.0, 0.0}}}; /*< Default energy mix if none is provided */
  double total_carbon_footprint = 0.0; /* Total CO2 emitted to produce the energy used by the host */ 
  double total_water_footprint = 0.0; /* Total water used to produce the energy used by the host */
  double current_weighted_carbon_intensity = 0.0; /* Current weighted carbon intensity of the energy mix (g CO2/kWh) */
  double current_weighted_water_intensity = 0.0; /* Current weighted water intensity of the energy mix (L/kWh) */

  std::map<std::string, double> parse_string_into_map(const char* input_cstr);
  void build_energy_mix(const std::map<std::string, double>& energy_percentage_map,
                        const std::map<std::string, double>& carbon_intensity_map,
                        const std::map<std::string, double>& water_intensity_map);
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

  double carbon_this_step = energy_this_step_kwh * this->current_weighted_carbon_intensity;
  double water_this_step = energy_this_step_kwh * this->current_weighted_water_intensity;
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
  const char* raw_carbon_intensity = host_->get_property("carbon_intensity");
  const char* raw_water_intensity = host_->get_property("water_intensity");

  if (raw_carbon_intensity == nullptr && raw_water_intensity == nullptr) {
    XBT_WARN("Host '%s': Missing values for properties 'carbon_intensity' and 'water_intensity', using default null energy mix.", host_->get_cname());
    this->energy_mix = this->default_energy_mix; // Default to 0 g/kWh and 0 L/kWh if not provided
  } else {
    std::map<std::string, double> energy_percentage_map = this->parse_string_into_map(raw_energy_mix);
    std::map<std::string, double> carbon_intensity_map = this->parse_string_into_map(raw_carbon_intensity);
    std::map<std::string, double> water_intensity_map = this->parse_string_into_map(raw_water_intensity);

    this->build_energy_mix(energy_percentage_map, carbon_intensity_map, water_intensity_map);
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

double HostEnvironmentalFootprint::get_host_carbon_intensity() 
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  return this->current_weighted_carbon_intensity;
}

double HostEnvironmentalFootprint::get_host_water_intensity() 
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  return this->current_weighted_water_intensity;
}


void HostEnvironmentalFootprint::set_host_energy_mix(const std::map<std::string, EnergySource>& mix)
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  this->energy_mix = mix;
  this->validate_energy_mix_composition();
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

void HostEnvironmentalFootprint::set_host_energy_mix_composition(const std::map<std::string, double>& composition)
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  // Zero out all existing percentages so stale sources (including NULL_SOURCE
  // from the default mix) don't accumulate alongside the new composition.
  for (auto& [source_name, source_info] : this->energy_mix)
    source_info.percentage = 0.0;

  for (const auto& [source_name, percentage] : composition) {
    if (this->energy_mix.find(source_name) != this->energy_mix.end()) {
      this->energy_mix[source_name].percentage = percentage;
    } else {
      EnergySource source_info;
      source_info.percentage = percentage;
      source_info.carbon_intensity = 0.0;
      source_info.water_intensity = 0.0;
      this->energy_mix[source_name] = source_info;
    }
  }

  this->validate_energy_mix_composition();
}

void HostEnvironmentalFootprint::set_carbon_intensities(const std::map<std::string, double>& intensities) 
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  for (const auto& [source_name, intensity] : intensities) {
    if (this->energy_mix.find(source_name) != this->energy_mix.end())
      this->energy_mix[source_name].carbon_intensity = intensity;
  }

  this->validate_energy_mix_composition();
}

void HostEnvironmentalFootprint::set_water_intensities(const std::map<std::string, double>& intensities) 
{
  if (this->last_updated < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnvironmentalFootprint::update, this));

  for (const auto& [source_name, intensity] : intensities) {
    if (this->energy_mix.find(source_name) != this->energy_mix.end())
      this->energy_mix[source_name].water_intensity = intensity;
  }

  this->validate_energy_mix_composition();
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
                      const std::map<std::string, double>& carbon_intensity_map,
                      const std::map<std::string, double>& water_intensity_map)
{
  for (const auto& [source_name, percentage] : energy_percentage_map) {
    double carbon_intensity = 0.0, water_intensity = 0.0;

    auto carbon_it = carbon_intensity_map.find(source_name);
    if (carbon_it != carbon_intensity_map.end()) {
      carbon_intensity = carbon_it->second;
    } else {
      XBT_WARN("Warning: Missing carbon intensity for source '%s' on host '%s'. Using 0 g/kWh.", source_name.c_str(), host_->get_cname());
    }

    auto water_it = water_intensity_map.find(source_name);
    if (water_it != water_intensity_map.end()) {
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
  double new_weighted_carbon_intensity = 0.0;
  double new_weighted_water_intensity = 0.0;
  double total = 0.0;
  for (const auto& [source_name, source_info] : this->energy_mix) {
    total += source_info.percentage;
    new_weighted_carbon_intensity += (source_info.carbon_intensity * source_info.percentage) / 100.0;
    new_weighted_water_intensity += (source_info.water_intensity * source_info.percentage) / 100.0;
  }

  if (total > 0 && !double_equals(total, 100.0, 1E-9)) {
      XBT_WARN("Host '%s' eco mix sums to %.2f%%, not 100%%. Using default emission mix.", host_->get_cname(), total);
      this->energy_mix = this->default_energy_mix;
      new_weighted_carbon_intensity = 0.0; 
      new_weighted_water_intensity  = 0.0;
  }

  this->current_weighted_carbon_intensity = new_weighted_carbon_intensity;
  this->current_weighted_water_intensity = new_weighted_water_intensity;
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

double sg_host_get_water_intensity(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_host_water_intensity();
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

void sg_host_set_energy_mix_composition(const_sg_host_t host, const std::map<std::string, double>& composition)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_host_energy_mix_composition(composition);
}

void sg_host_set_carbon_intensities(const_sg_host_t host, const std::map<std::string, double>& intensities)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_carbon_intensities(intensities);
}

void sg_host_set_water_intensities(const_sg_host_t host, const std::map<std::string, double>& intensities)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_water_intensities(intensities);
}
