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
  e.seal_platform();
  
  SECTION("Initial footprints are zero")
  {
    REQUIRE(sg_host_get_carbon_footprint(host) == 0.0);
    REQUIRE(sg_host_get_water_footprint(host) == 0.0);
  } 
}


TEST_CASE("plugins::host_environmental_footprint: Footprint accumulation", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
  
  sg_host_set_carbon_intensity(host, 1000.0);
  sg_host_set_water_intensity(host, 1500.0);
  sg_host_set_pue(host, 1.0);
  sg_host_set_wue(host, 1.5);
  
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
  e.seal_platform();
  
  SECTION("Carbon footprint calculated correctly (Off-site only)")
  {
    sg_host_set_carbon_intensity(host, 1000.0); // 1000 g CO2/kWh
    sg_host_set_pue(host, 1.5);
    
    // Execute work: 1 GFlop at 1 GFlop/s = 1 second
    // Power consumption: 100W (from wattage_per_state)
    // Energy: 100W * 1/3600 s = 1/36 Wh = 1/36000 kWh
    // Expected CO2 with PUE 1.5: 1/36000 kWh * 1000 g/kWh * 1.5 = 1/24 g =~ 0.041667
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double carbon = sg_host_get_carbon_footprint(host);
    REQUIRE(carbon == Approx(0.041667).margin(0.000001)); // ±0.000001g tolerance
  }
  
  SECTION("Water footprint calculated correctly (Off-site + On-site)")
  {
    sg_host_set_water_intensity(host, 1500.0); // 1500 L/kWh
    sg_host_set_wue(host, 2.0);
    sg_host_set_pue(host, 1.5);
  
    // Execute work: 1 GFlop at 1 GFlop/s = 1 second
    // Energy: 100W * 1/3600 s = 1/36 Wh = 1/36000 kWh
    // Expected water on-site with WUE 2.0: 1/36000 kWh * 2.0 = 1/18000 L = 0.000055556 L
    // Expected water off-site with PUE 1.5: 1/36000 kWh * 1.5 * 1500 L/kWh = 1/24 L = 0.041667 L
    // Total expected water: 0.041667 + 0.00055556 = 0.041722 L
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double water = sg_host_get_water_footprint(host);
    REQUIRE(water == Approx(0.041722).margin(0.000001)); // ±0.000001L tolerance
  }
  
  SECTION("Zero intensity sources produce zero footprint (excluding on-site/embodied)")
  {
    sg_host_set_carbon_intensity(host, 0.0);
    sg_host_set_water_intensity(host, 0.0);
    sg_host_set_wue(host, 0.0);
    
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    REQUIRE(sg_host_get_carbon_footprint(host) == 0.0);
    REQUIRE(sg_host_get_water_footprint(host) == 0.0);
  }
}

TEST_CASE("plugins::host_environmental_footprint: PUE impact on offsite footprint", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
  
  sg_host_set_carbon_intensity(host, 1000.0); // 1000 g/kWh
  sg_host_set_pue(host, 2.0); // PUE of 2.0 should double the off-site footprint
  
  // Execute work: 1 GFlop at 1 GFlop/s = 1 second
  // Power consumption: 100W (from wattage_per_state)
  // Energy: 100W * 1/3600 s = 1/36 Wh = 1/36000 kWh
  // Expected CO2 with PUE 2.0: 1/36000 kWh * 1000 g/kWh * 2.0 = 1/18 g =~ 0.055556
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
  e.run();
  
  REQUIRE(sg_host_get_carbon_footprint(host) == Approx(0.055556).margin(0.000001));
}

TEST_CASE("plugins::host_environmental_footprint: WUE impact and dynamic update", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
  
  sg_host_set_water_intensity(host, 0.0); // No off-site water use
  
  double water_mid = 0.0;
  
  host->add_actor("worker", [&]() {
    // First phase: WUE = 1.8
    sg_host_set_wue(host, 1.8);
    simgrid::s4u::this_actor::execute(1e9); // 1s
    
    water_mid = sg_host_get_water_footprint(host);
    
    // Second phase: WUE = 0.3 
    sg_host_set_wue(host, 0.3);
    simgrid::s4u::this_actor::execute(1e9); // +1s
  });
  
  e.run();
  
  double water_end = sg_host_get_water_footprint(host);
  
  // First phase (with WUE 1.8): (1/36000) * 1.8 = 0.000050 L
  REQUIRE(water_mid == Approx(0.000050).margin(0.000001));
  
  // Second phase (with WUE 0.3): (1/36000) * 0.3 = 0.0000083 L
  // Expected total = 0.0000583 L
  REQUIRE(water_end == Approx(0.0000583).margin(0.000001));
}

TEST_CASE("plugins::host_environmental_footprint: Embodied footprint amortization", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* root = e.get_netzone_root();
  auto* zone = root->add_netzone_full("test_zone"); 
  auto* host = zone->add_host("host_embodied", 1e9);
  
  host->set_property("wattage_per_state", "100.0:100.0:100.0");
  host->set_property("embodied_carbon", "7200.0"); // 7200g
  host->set_property("host_lifetime", "3600.0");   // 1 hour of useful life
  host->seal();
  zone->seal();
  e.seal_platform();
  
  sg_host_set_carbon_intensity(host, 0.0); // No operational carbon, only embodied
  
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::sleep_for(10.0);
  });
  e.run();
  
  // Fraction of life used: 10s / 3600s
  // Expected carbon = (10/3600) * 7200 = 20.0 g
  REQUIRE(sg_host_get_carbon_footprint(host) == Approx(20.0).margin(0.000001));
}


TEST_CASE("plugins::host_environmental_footprint: Getters work when called from actor during run", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();

  sg_host_set_carbon_intensity(host, 1000.0);
  sg_host_set_water_intensity(host, 1500.0);
  sg_host_set_pue(host, 1.0);
  sg_host_set_wue(host, 1.5);

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
