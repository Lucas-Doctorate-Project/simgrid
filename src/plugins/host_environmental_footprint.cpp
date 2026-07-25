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

This is the environmental footprint plugin. It tracks the operational carbon emissions and water
consumption attributable to each SimGrid host over the course of a simulation. It also stores the
fixed embodied carbon and water footprints of the host.

The plugin exposes five distinct contributions:

- **Operational carbon** (\f$F^{\text{C}}_{\text{op}}\f$): off-site CO₂ from generating the grid
  electricity drawn by the host and its supporting datacenter infrastructure, accumulated
  incrementally during the simulation.
- **Embodied carbon** (\f$F^{\text{C}}_{\text{emb}}\f$): CO₂ emitted during host manufacturing,
  stored as a fixed host inventory value.
- **On-site water** (\f$F^{\text{W}}_{\text{on}}\f$): water consumed locally for cooling.
- **Off-site water** (\f$F^{\text{W}}_{\text{off}}\f$): water consumed off-site to generate the
  electricity used by the host (e.g. for thermoelectric cooling or hydropower evaporation).
- **Embodied water** (\f$F^{\text{W}}_{\text{emb}}\f$): water consumed during host manufacturing,
  stored as a fixed host inventory value.

Let \f$E\f$ be the IT energy consumed by the host during a time step \f$\Delta t\f$ (in kWh, tracked
by the energy plugin), \f$\text{PUE}\f$ the datacenter Power Usage Effectiveness, \f$\text{WUE}\f$
the Water Usage Effectiveness (L/kWh of IT energy), \f$I^{\text{C}}\f$ and \f$I^{\text{W}}\f$ the
carbon and water intensities of the grid (g CO₂/kWh and L/kWh respectively). Each step contributes:

\f[
\begin{aligned}
\Delta F^{\text{C}}_{\text{op}}  &= E \cdot \text{PUE} \cdot I^{\text{C}} \\
\Delta F^{\text{W}}_{\text{on}}  &= E \cdot \text{WUE} \\
\Delta F^{\text{W}}_{\text{off}} &= E \cdot \text{PUE} \cdot I^{\text{W}}
\end{aligned}
\f]

The general carbon and water footprint getters report operational impacts only:

\f[
F^{\text{C}}_{\text{total}} = F^{\text{C}}_{\text{op}}, \qquad
F^{\text{W}}_{\text{total}} = F^{\text{W}}_{\text{on}} + F^{\text{W}}_{\text{off}}
\f]

The fixed embodied values are kept separate so callers can apply an allocation policy appropriate
to their own accounting boundary.

To activate this plugin, call :cpp:func:`sg_host_environmental_footprint_plugin_init()` before
loading your platform. The footprints are then updated automatically whenever the host changes
state, changes speed, or one of the getters is queried.

Use :cpp:func:`sg_host_get_carbon_footprint()` and :cpp:func:`sg_host_get_water_footprint()` to
retrieve the operational totals, or one of the per-component getters for the breakdown:

- :cpp:func:`sg_host_get_carbon_operational_footprint()`
- :cpp:func:`sg_host_get_carbon_embodied_footprint()`
- :cpp:func:`sg_host_get_water_onsite_footprint()`
- :cpp:func:`sg_host_get_water_offsite_footprint()`
- :cpp:func:`sg_host_get_water_embodied_footprint()`

The fixed embodied values can be replaced at runtime via
:cpp:func:`sg_host_set_carbon_embodied_footprint()` and
:cpp:func:`sg_host_set_water_embodied_footprint()`.

Carbon and water intensities, PUE and WUE can be inspected and overridden at runtime via
:cpp:func:`sg_host_get_carbon_intensity()` / :cpp:func:`sg_host_set_carbon_intensity()` and the
analogous water, PUE, and WUE accessors. This is useful for modelling time-varying grid mixes or
datacenter conditions.

The model is configured per host via the following XML properties (all optional, defaulting to
neutral values that disable the corresponding contribution):

  - ``carbon_intensity`` — grid carbon intensity in g CO₂/kWh.
  - ``water_intensity`` — grid water intensity in L/kWh.
  - ``pue`` — Power Usage Effectiveness (dimensionless, ≥ 1; defaults to 1.0).
  - ``wue`` — Water Usage Effectiveness in L/kWh of IT energy (defaults to 0.0).
  - ``embodied_carbon`` — total g CO₂ embodied in the host's manufacturing.
  - ``embodied_water`` — total L of water embodied in the host's manufacturing.

Carbon and water intensities are expected to be pre-aggregated over the energy mix of the host's
grid. A simple weighted average works well, where given \f$N\f$ sources with intensities \f$I_j\f$
and shares \f$w_j\f$ (with \f$\sum_j w_j = 1\f$):

\f[
  \bar{I} = \sum_{j=1}^{N} I_j \cdot w_j
\f]

Here is an example of XML declaration:

.. code-block:: xml

   <host id="Host1" speed="100.0Mf, 1e-9Mf, 0.5f, 0.05f" pstate="0">
       <prop id="wattage_per_state" value="30.0:30.0:100.0, 9.75:9.75:9.75, 200.996721311:200.996721311:200.996721311, 425.1743849:425.1743849:425.1743849" />
       <prop id="wattage_off" value="9.75" />
       <prop id="carbon_intensity" value="475" />
       <prop id="water_intensity" value="1200" />
       <prop id="pue" value="1.4" />
       <prop id="wue" value="1.8" />
       <prop id="embodied_carbon" value="320000" />
       <prop id="embodied_water" value="2500000" />
   </host>


### How accurate are these models?
This model is still a work in progress and may not fully reflect real-world CO₂ emissions or water
use. The accuracy of the results depends on the quality of the input parameters, in particular the
host's power profile, the grid intensities, and the embodied impact estimates. Further improvements
and evaluations of the model are needed. Keep this in mind when using this plugin.

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
  double get_carbon_operational();
  double get_carbon_embodied();
  void set_carbon_embodied(double value);
  double get_water_onsite();
  double get_water_offsite();
  double get_water_embodied();
  void set_water_embodied(double value);
  double get_host_carbon_intensity();
  void set_carbon_intensity(double new_intensity);
  double get_host_water_intensity();
  void set_water_intensity(double new_intensity);
  double get_last_update_time() const { return last_updated_; }
  void set_pue(double new_pue);
  double get_pue() const { return pue_; }
  void set_wue(double new_wue);
  double get_wue() const { return wue_; }
  void update();
private:
  simgrid::s4u::Host* host_ = nullptr;

  double last_updated_ = simgrid::s4u::Engine::get_clock(); /*< Timestamp of the last update event*/
  
  double pue_ = 1.0;           // Power Usage Effectiveness (default: ideal)
  double wue_ = 0.0;           // Water Usage Effectiveness (L/kWh) - on-site cooling

  double total_carbon_footprint_ = 0.0; /* Operational CO2 emitted to produce the energy used by the host */
  double total_water_footprint_ = 0.0; /* Operational water used by the host and its energy supply */

  double carbon_operational_ = 0.0; /* Off-site CO2 from grid electricity */
  double carbon_embodied_    = 0.0; /* Fixed embodied CO2 from host manufacturing */
  double water_onsite_       = 0.0; /* On-site cooling water */
  double water_offsite_      = 0.0; /* Off-site water for grid electricity generation */
  double water_embodied_     = 0.0; /* Fixed embodied water from host manufacturing */
  
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

  this->carbon_operational_ += total_datacenter_energy_this_step_kwh * this->carbon_intensity_;

  this->water_onsite_   += computation_energy_this_step_kwh * this->wue_;
  this->water_offsite_  += total_datacenter_energy_this_step_kwh * this->water_intensity_;

  this->total_carbon_footprint_ = this->carbon_operational_;
  this->total_water_footprint_  = this->water_onsite_ + this->water_offsite_;

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
    this->carbon_embodied_ = std::stod(raw_emb_carbon);

  const char* raw_emb_water = host_->get_property("embodied_water");
  if (raw_emb_water != nullptr)
    this->water_embodied_ = std::stod(raw_emb_water);

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

double HostEnvironmentalFootprint::get_carbon_operational()
{
  this->ensure_up_to_date();
  return this->carbon_operational_;
}

double HostEnvironmentalFootprint::get_carbon_embodied()
{
  return this->carbon_embodied_;
}

void HostEnvironmentalFootprint::set_carbon_embodied(double value)
{
  this->carbon_embodied_ = value;
}

double HostEnvironmentalFootprint::get_water_onsite()
{
  this->ensure_up_to_date();
  return this->water_onsite_;
}

double HostEnvironmentalFootprint::get_water_offsite()
{
  this->ensure_up_to_date();
  return this->water_offsite_;
}

double HostEnvironmentalFootprint::get_water_embodied()
{
  return this->water_embodied_;
}

void HostEnvironmentalFootprint::set_water_embodied(double value)
{
  this->water_embodied_ = value;
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


/** \ingroup plugin_environmental_footprint
 * \brief Enable the host environmental footprint plugin
 * \details Enable the environmental footprint plugin to get the operational and embodied footprint of each host.
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

/** @ingroup plugin_environmental_footprint
 *  @brief Returns the operational carbon footprint of the host so far (in g CO2eq)
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

/** @ingroup plugin_environmental_footprint
 *  @brief Returns the operational carbon footprint of the host so far (in g CO2eq)
 */
double sg_host_get_carbon_operational_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_carbon_operational();
}

/** @ingroup plugin_environmental_footprint
 *  @brief Returns the fixed embodied carbon footprint of the host (in g CO2eq)
 */
double sg_host_get_carbon_embodied_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_carbon_embodied();
}

/** @ingroup plugin_environmental_footprint
 *  @brief Replaces the fixed embodied carbon footprint of the host (in g CO2eq)
 */
void sg_host_set_carbon_embodied_footprint(const_sg_host_t host, double value)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_carbon_embodied(value);
}

double sg_host_get_water_onsite_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_water_onsite();
}

double sg_host_get_water_offsite_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_water_offsite();
}

/** @ingroup plugin_environmental_footprint
 *  @brief Returns the fixed embodied water footprint of the host (in L)
 */
double sg_host_get_water_embodied_footprint(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_water_embodied();
}

/** @ingroup plugin_environmental_footprint
 *  @brief Replaces the fixed embodied water footprint of the host (in L)
 */
void sg_host_set_water_embodied_footprint(const_sg_host_t host, double value)
{
  ensure_plugin_inited();
  host->extension<HostEnvironmentalFootprint>()->set_water_embodied(value);
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

double sg_host_get_pue(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_pue();
}

double sg_host_get_wue(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnvironmentalFootprint>()->get_wue();
}
