/* Copyright (c) 2017-2025. The SimGrid Team. All rights reserved.               */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include <src/3rd-party/catch.hpp>

#include <simgrid/s4u/Engine.hpp>
#include <simgrid/s4u/Actor.hpp>
#include <simgrid/plugins/environmental_footprint.h>
#include <simgrid/Exception.hpp>

static simgrid::s4u::Host* create_test_host(simgrid::s4u::Engine& e, const std::string& name = "test_host", double speed = 1e9)
{
  auto* root = e.get_netzone_root();
  auto* zone = root->add_netzone_full("test_zone"); 

  auto* host = zone->add_host(name, speed);

  host->set_property("wattage_per_state", "100.0:100.0:100.0");
  host->set_property("wattage_off", "10.0");

  host->seal();
  zone->seal();
  return host;
}

TEST_CASE("plugins::host_environmental_footprint: Basic functionality", "[plugin][enviromental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  
  SECTION("Initial footprints are zero")
  {
    REQUIRE(sg_host_get_carbon_footprint(host) == 0.0);
    REQUIRE(sg_host_get_water_footprint(host) == 0.0);
  }
  
  SECTION("Energy mix can be retrieved")
  {
    std::string mix = sg_host_get_energy_mix_formatted(host);
    REQUIRE_FALSE(mix.empty());
  }
}

TEST_CASE("plugins::host_environmental_footprint: Basic energy mix configuration", "[plugin][enviromental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  
  SECTION("Set complete energy mix")
  {
    std::map<std::string, EnergySource> mix = {
      {"Coal", {50.0, 1000.0, 1500.0}},
      {"Solar", {50.0, 50.0, 50.0}}
    };
    REQUIRE_NOTHROW(sg_host_set_energy_mix(host, mix));
  }
  
  SECTION("Set energy mix composition")
  {
    std::map<std::string, double> composition = {{"Coal", 70.0}, {"Hydro", 30.0}};
    REQUIRE_NOTHROW(sg_host_set_energy_mix_composition(host, composition));
  }
  
  SECTION("Set carbon and water intensities")
  {
    std::map<std::string, double> carbon = {{"Coal", 1000.0}};
    std::map<std::string, double> water = {{"Coal", 1500.0}};
    REQUIRE_NOTHROW(sg_host_set_carbon_intensities(host, carbon));
    REQUIRE_NOTHROW(sg_host_set_water_intensities(host, water));
  }

  SECTION("Invalid mix composition falls back to zero footprint")
  {
    // Build a mix that sums to 99% (invalid) with non-zero intensities
    std::map<std::string, EnergySource> bad_mix = {
        {"Coal",  {90.0, 1000.0, 1500.0}}, {"Solar", {9.0, 50.0, 50.0}}
    };

    sg_host_set_energy_mix(host, bad_mix);

    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    e.run();

    REQUIRE(sg_host_get_carbon_footprint(host) == Approx(0.0));
    REQUIRE(sg_host_get_water_footprint(host)  == Approx(0.0));
  }
}

TEST_CASE("plugins::host_environmental_footprint: Footprint accumulation", "[plugin][enviromental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  
  std::map<std::string, EnergySource> mix = {{"Coal", {100.0, 1000.0, 1500.0}}};
  sg_host_set_energy_mix(host, mix);
  
  double carbon_before = sg_host_get_carbon_footprint(host);
  double water_before = sg_host_get_water_footprint(host);
  
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
  
  e.run();
  
  double carbon_after = sg_host_get_carbon_footprint(host);
  double water_after = sg_host_get_water_footprint(host);
  
  REQUIRE(carbon_after > carbon_before);
  REQUIRE(water_after > water_before);
}


TEST_CASE("plugins::host_environmental_footprint: Footprint calculation accuracy", "[plugin][enviromental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  
  SECTION("Carbon footprint calculated correctly with known energy consumption")
  {
    // Set 100% coal with 1000 g CO2/kWh
    std::map<std::string, EnergySource> mix = {{"Coal", {100.0, 1000.0, 0.0}}};
    sg_host_set_energy_mix(host, mix);
    
    // Execute work: 1 GFlop at 1 GFlop/s = 1 second
    // Power consumption: 100W (from wattage_per_state)
    // Energy: 100W * 1/3600 s = 1/36 Wh = 1/36000 kWh
    // Expected CO2: 1/36000 kWh * 1000 g/kWh = 1/36 g =~ 0.027778
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double carbon = sg_host_get_carbon_footprint(host);
    REQUIRE(carbon == Approx(0.027778).margin(0.000001)); // ±0.000001g tolerance
  }
  
  SECTION("Water footprint calculated correctly with known energy consumption")
  {
    // Set 100% coal with 1500 L/kWh
    std::map<std::string, EnergySource> mix = {{"Coal", {100.0, 0.0, 1500.0}}};
    sg_host_set_energy_mix(host, mix);
    
    // Execute work: 1 GFlop at 1 GFlop/s = 1 second
    // Energy: 100W * 1/3600 s = 1/36 Wh = 1/36000 kWh
    // Expected water: 1/36000 kWh * 1500 L/kWh = 0.04166667 L
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double water = sg_host_get_water_footprint(host);
    REQUIRE(water == Approx(0.041667).margin(0.000001)); // ±0.000001L tolerance
  }
  
  SECTION("Mixed energy sources calculate weighted average correctly")
  {
    // 50% Coal (1000 g/kWh) + 50% Solar (50 g/kWh) = 525 g/kWh weighted average
    // 50% Coal (1500 L/kWh) + 50% Solar (100 L/kWh) = 800 L/kWh weighted average
    std::map<std::string, EnergySource> mix = {
      {"Coal", {50.0, 1000.0, 1500.0}},
      {"Solar", {50.0, 50.0, 100.0}}
    };
    sg_host_set_energy_mix(host, mix);
    
    // Execute work: energy = 1/36000 kWh
    // Expected CO2: 1/36000 kWh * 525 g/kWh = 0.014583 g
    // Expected water: 1/36000 kWh * 800 L/kWh = 0.022222 L
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double carbon = sg_host_get_carbon_footprint(host);
    double water = sg_host_get_water_footprint(host);
    
    REQUIRE(carbon == Approx(0.014583).margin(0.000001)); 
    REQUIRE(water == Approx(0.022222).margin(0.000001)); 
  }
  
  SECTION("Zero intensity sources produce zero footprint")
  {
    std::map<std::string, EnergySource> mix = {{"Clean", {100.0, 0.0, 0.0}}};
    sg_host_set_energy_mix(host, mix);
    
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    REQUIRE(sg_host_get_carbon_footprint(host) == 0.0);
    REQUIRE(sg_host_get_water_footprint(host) == 0.0);
  }
}


TEST_CASE("plugins::host_environmental_footprint: Get methods return correct values", "[plugin][enviromental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  
  SECTION("Energy mix formatted output contains set values")
  {
    std::map<std::string, EnergySource> mix = {
      {"Coal", {60.0, 1000.0, 1500.0}},
      {"Hydro", {25.0, 24.0, 100.0}},
      {"Solar", {15.0, 50.0, 50.0}}
    };
    sg_host_set_energy_mix(host, mix);
    
    std::string formatted = sg_host_get_energy_mix_formatted(host);
    
    // Check that all sources are present in the output
    REQUIRE(formatted.find("Coal") != std::string::npos);
    REQUIRE(formatted.find("Hydro") != std::string::npos);
    REQUIRE(formatted.find("Solar") != std::string::npos);
    
    // Check that percentages are present
    REQUIRE(formatted.find("60") != std::string::npos);
    REQUIRE(formatted.find("25") != std::string::npos);
    REQUIRE(formatted.find("15") != std::string::npos);
    
    // Check that intensities are present
    REQUIRE(formatted.find("1000") != std::string::npos);
    REQUIRE(formatted.find("1500") != std::string::npos);
  }
  
  SECTION("Modified composition is reflected in formatted output")
  {
    std::map<std::string, EnergySource> mix = {
      {"Coal", {50.0, 1000.0, 1500.0}},
      {"Solar", {50.0, 50.0, 50.0}}
    };
    sg_host_set_energy_mix(host, mix);
    
    // Modify composition
    std::map<std::string, double> new_composition = {
      {"Coal", 30.0},
      {"Solar", 70.0}
    };
    sg_host_set_energy_mix_composition(host, new_composition);
    
    std::string formatted = sg_host_get_energy_mix_formatted(host);
    
    // Check new percentages are reflected
    REQUIRE(formatted.find("30") != std::string::npos);
    REQUIRE(formatted.find("70") != std::string::npos);
  }
  
  SECTION("Modified intensities affect footprint calculations")
  { 
    // Start with 100% coal at half intensities
    std::map<std::string, EnergySource> mix = {{"Coal", {100.0, 500.0, 750.0}}};
    sg_host_set_energy_mix(host, mix);

    double carbon_after_first = 0.0;
    double water_after_first  = 0.0;
    double carbon_after_second = 0.0;
    double water_after_second  = 0.0;

    host->add_actor("two_phase_worker", [&]() {
      // Phase 1: run workload
      simgrid::s4u::this_actor::execute(1e9);

      carbon_after_first = sg_host_get_carbon_footprint(host);
      water_after_first  = sg_host_get_water_footprint(host);

      // Update intensities
      sg_host_set_carbon_intensities(host, {{"Coal", 1000.0}}); // 2x carbon
      sg_host_set_water_intensities(host,  {{"Coal", 1500.0}}); // 2x water

      // Phase 2: run same workload again
      simgrid::s4u::this_actor::execute(1e9);

      carbon_after_second = sg_host_get_carbon_footprint(host);
      water_after_second  = sg_host_get_water_footprint(host);
    });

    e.run();

    // Compare increments
    const double carbon_delta1 = carbon_after_first;
    const double carbon_delta2 = carbon_after_second - carbon_after_first;

    const double water_delta1 = water_after_first;
    const double water_delta2 = water_after_second - water_after_first;

    REQUIRE(carbon_delta2 == Approx(2.0 * carbon_delta1).margin(1e-9));
    REQUIRE(water_delta2  == Approx(2.0 * water_delta1).margin(1e-9));
  }
  
  SECTION("Footprint getters are idempotent")
  {
    std::map<std::string, EnergySource> mix = {{"Coal", {100.0, 1000.0, 1500.0}}};
    sg_host_set_energy_mix(host, mix);
    
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    e.run();
    
    // Multiple calls should return same value
    double carbon1 = sg_host_get_carbon_footprint(host);
    double carbon2 = sg_host_get_carbon_footprint(host);
    double carbon3 = sg_host_get_carbon_footprint(host);
    
    REQUIRE(carbon1 == carbon2);
    REQUIRE(carbon2 == carbon3);
    
    double water1 = sg_host_get_water_footprint(host);
    double water2 = sg_host_get_water_footprint(host);
    double water3 = sg_host_get_water_footprint(host);
    
    REQUIRE(water1 == water2);
    REQUIRE(water2 == water3);
  }
}

TEST_CASE("plugins::host_environmental_footprint: Getters work when called from actor during run", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);

  std::map<std::string, EnergySource> mix = {
      {"Coal", {100.0, 1000.0, 1500.0}}
  };
  sg_host_set_energy_mix(host, mix);

  double carbon_mid = 0.0;
  double water_mid  = 0.0;

  host->add_actor("worker", [&]() {
    // Do some work first so footprint should be > 0 afterwards
    simgrid::s4u::this_actor::execute(1e9);

    carbon_mid = sg_host_get_carbon_footprint(host);
    water_mid  = sg_host_get_water_footprint(host);

    // Do more work so end value should be >= mid value
    simgrid::s4u::this_actor::execute(1e9);
  });

  e.run();

  const double carbon_end = sg_host_get_carbon_footprint(host);
  const double water_end  = sg_host_get_water_footprint(host);

  REQUIRE(carbon_mid > 0.0);
  REQUIRE(water_mid  > 0.0);

  REQUIRE(carbon_end >= carbon_mid);
  REQUIRE(water_end  >= water_mid);
}
