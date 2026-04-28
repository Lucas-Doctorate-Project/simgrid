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

// ============================================================
// 1. Basic functionality
// ============================================================


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

// ============================================================
// 2. Footprint accumulation (qualitative)
// ============================================================

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

// ============================================================
// 3. Footprint calculation accuracy
// ============================================================

TEST_CASE("plugins::host_environmental_footprint: Footprint calculation accuracy", "[plugin][enviromental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
  
  SECTION("Carbon footprint calculated correctly (Off-site only, PUE 1.5)")
  {
    // Execute 1 GFlop at 1 GFlop/s  →  1 second
    // Power: 100 W  →  Energy = 100 J = 100/3.6e6 kWh = 1/36000 kWh
    // Total datacenter energy with PUE 1.5: 1/36000 * 1.5 = 1/24000 kWh
    // CO2 = 1/24000 * 1000 g/kWh = 1/24 g ≈ 0.041667 g
    sg_host_set_carbon_intensity(host, 1000.0); 
    sg_host_set_pue(host, 1.5);

    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double carbon = sg_host_get_carbon_footprint(host);
    REQUIRE(carbon == Approx(0.041667).margin(0.000001)); // ±0.000001g tolerance
  }
  
  SECTION("Water footprint calculated correctly (Off-site + On-site)")
  {
    // Energy (IT): 1/36000 kWh
    // Off-site (PUE 1.5): 1/36000 * 1.5 * 1500 L/kWh = 0.062500 L
    // On-site  (WUE 2.0): 1/36000 * 2.0              = 0.000056 L
    // Total ≈ 0.062556 L
    sg_host_set_water_intensity(host, 1500.0); // 1500 L/kWh
    sg_host_set_wue(host, 2.0);
    sg_host_set_pue(host, 1.5);
  
    host->add_actor("worker", []() {
      simgrid::s4u::this_actor::execute(1e9);
    });
    
    e.run();
    
    double water = sg_host_get_water_footprint(host);
    REQUIRE(water == Approx(0.062556).margin(0.000001)); // ±0.000001L tolerance
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

// ============================================================
// 4. PUE impact on off-site footprint
// ============================================================

TEST_CASE("plugins::host_environmental_footprint: PUE impact on offsite footprint", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
  
  // PUE 2.0 → doubles off-site carbon vs. ideal PUE 1.0
  // Energy (IT): 1/36000 kWh
  // CO2 = 1/36000 * 1000 * 2.0 = 1/18 g ≈ 0.055556 g
  sg_host_set_carbon_intensity(host, 1000.0); // 1000 g/kWh
  sg_host_set_pue(host, 2.0);
  
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
  e.run();
  
  REQUIRE(sg_host_get_carbon_footprint(host) == Approx(0.055556).margin(0.000001));
}

// ============================================================
// 4b. PUE = 1.0 must NOT amplify footprint
// ============================================================

TEST_CASE("plugins::host_environmental_footprint: PUE=1.0 is the ideal baseline", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
 
  // With PUE=1.0 the datacenter energy equals the IT energy.
  // CO2 = 1/36000 kWh * 1000 g/kWh * 1.0 = 1/36 g ≈ 0.027778 g
  sg_host_set_carbon_intensity(host, 1000.0);
  sg_host_set_pue(host, 1.0);
 
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
  e.run();
 
  REQUIRE(sg_host_get_carbon_footprint(host) == Approx(0.027778).margin(0.000001));
}

// ============================================================
// 5. WUE dynamic update during simulation
// ============================================================

TEST_CASE("plugins::host_environmental_footprint: WUE impact and dynamic update", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
  
  sg_host_set_water_intensity(host, 0.0); // No off-site water use
  
  double water_mid = 0.0;
  
  host->add_actor("worker", [&]() {
    // Phase 1 – WUE = 1.8, execute 1 s
    // On-site water = 1/36000 * 1.8 = 0.000050 L
    sg_host_set_wue(host, 1.8);
    simgrid::s4u::this_actor::execute(1e9);
 
    water_mid = sg_host_get_water_footprint(host);
 
    // Phase 2 – WUE = 0.3, execute 1 s
    // On-site water = 1/36000 * 0.3 ≈ 0.0000083 L
    sg_host_set_wue(host, 0.3);
    simgrid::s4u::this_actor::execute(1e9);
  });
 
  e.run();
 
  double water_end = sg_host_get_water_footprint(host);
 
  REQUIRE(water_mid == Approx(0.000050).margin(0.000001));
  // Total = 0.000050 + 0.0000083... ≈ 0.0000583 L
  REQUIRE(water_end == Approx(0.0000583).margin(0.000001));
}

// ============================================================
// 6. Embodied footprint amortization
// ============================================================

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

// ============================================================
// 6b. Operational footprint without embodied
// ============================================================
 
TEST_CASE("plugins::host_environmental_footprint: No embodied when lifetime not set", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e); // host_lifetime not set → defaults to 0
  e.seal_platform();
 
  sg_host_set_carbon_intensity(host, 1000.0);
  sg_host_set_pue(host, 1.0);
 
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
  e.run();
 
  // With host_lifetime = 0, the ternary in update() yields 0 for lifetime_fraction.
  // Therefore the embodied term is zero and the result must equal the pure
  // operational footprint: 1/36000 * 1000 * 1.0 ≈ 0.027778 g.
  REQUIRE(sg_host_get_carbon_footprint(host) == Approx(0.027778).margin(0.000001));
}
 
// ============================================================
// 6c. host_lifetime = 0 must not cause division by zero
// ============================================================
 
TEST_CASE("plugins::host_environmental_footprint: host_lifetime=0 does not divide by zero", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
 
  // Explicitly confirm zero lifetime produces zero embodied (no crash)
  sg_host_set_carbon_intensity(host, 0.0);
  sg_host_set_water_intensity(host, 0.0);
 
  host->add_actor("worker", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
 
  REQUIRE_NOTHROW(e.run());
  REQUIRE(sg_host_get_carbon_footprint(host) == 0.0);
  REQUIRE(sg_host_get_water_footprint(host)  == 0.0);
}

// ============================================================
// 7. Getters called from actor mid-run
// ============================================================

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

// ============================================================
// 8. Multiple independent hosts do not contaminate each other
// ============================================================
 
TEST_CASE("plugins::host_environmental_footprint: Multiple hosts are independent", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
 
  auto* root = e.get_netzone_root();
 
  auto* zone_a = root->add_netzone_full("zone_a");
  auto* host_a = zone_a->add_host("host_a", 1e9);
  host_a->set_property("wattage_per_state", "100.0:100.0:100.0");
  host_a->set_property("wattage_off", "10.0");
  host_a->seal();
  zone_a->seal();
 
  auto* zone_b = root->add_netzone_full("zone_b");
  auto* host_b = zone_b->add_host("host_b", 1e9);
  host_b->set_property("wattage_per_state", "100.0:100.0:100.0");
  host_b->set_property("wattage_off", "10.0");
  host_b->seal();
  zone_b->seal();
 
  e.seal_platform();
 
  // host_a: high carbon intensity; host_b: zero intensity
  sg_host_set_carbon_intensity(host_a, 1000.0);
  sg_host_set_pue(host_a, 1.0);
 
  sg_host_set_carbon_intensity(host_b, 0.0);
  sg_host_set_pue(host_b, 1.0);
 
  host_a->add_actor("worker_a", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
  host_b->add_actor("worker_b", []() {
    simgrid::s4u::this_actor::execute(1e9);
  });
 
  e.run();
 
  // host_b must have zero carbon footprint despite host_a emitting CO2
  REQUIRE(sg_host_get_carbon_footprint(host_b) == 0.0);
  // host_a must have nonzero footprint unaffected by host_b's zero intensity
  REQUIRE(sg_host_get_carbon_footprint(host_a) == Approx(0.027778).margin(0.000001));
}
 
// ============================================================
// 9. Dynamic change of carbon_intensity mid-simulation
// ============================================================
 
TEST_CASE("plugins::host_environmental_footprint: Dynamic carbon_intensity update", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
 
  sg_host_set_pue(host, 1.0);
 
  double carbon_after_phase1 = 0.0;
 
  host->add_actor("worker", [&]() {
    // Phase 1: intensity = 1000 g/kWh, 1 s of work
    // CO2 = 1/36000 * 1000 ≈ 0.027778 g
    sg_host_set_carbon_intensity(host, 1000.0);
    simgrid::s4u::this_actor::execute(1e9);
 
    carbon_after_phase1 = sg_host_get_carbon_footprint(host);
 
    // Phase 2: intensity = 0, 1 s of work → zero additional CO2
    sg_host_set_carbon_intensity(host, 0.0);
    simgrid::s4u::this_actor::execute(1e9);
  });
 
  e.run();
 
  double carbon_end = sg_host_get_carbon_footprint(host);
 
  REQUIRE(carbon_after_phase1 == Approx(0.027778).margin(0.000001));
  // Final footprint must equal phase-1 footprint (phase 2 adds nothing)
  REQUIRE(carbon_end == Approx(carbon_after_phase1).margin(0.000001));
}
 
// ============================================================
// 10. Two actors on the same host (load sharing, not doubling)
// ============================================================
 
TEST_CASE("plugins::host_environmental_footprint: Two actors on same host share load", "[plugin][environmental_footprint]")
{
  sg_host_environmental_footprint_plugin_init();
  simgrid::s4u::Engine e("test");
  auto* host = create_test_host(e);
  e.seal_platform();
 
  sg_host_set_carbon_intensity(host, 1000.0);
  sg_host_set_pue(host, 1.0);
 
  // Both actors compete for the same CPU: each gets half the speed.
  // Each executes 0.5e9 flops at 0.5 GFlop/s → each takes 1 second, both finish at t=1.
  // During that 1 s the host runs at 100 W (shared, not doubled).
  // CO2 = 1/36000 kWh * 1000 g/kWh ≈ 0.027778 g  (same as a single actor for 1 s)
  host->add_actor("worker1", []() {
    simgrid::s4u::this_actor::execute(0.5e9);
  });
  host->add_actor("worker2", []() {
    simgrid::s4u::this_actor::execute(0.5e9);
  });
 
  e.run();
 
  // Footprint must reflect 1 s of host-level power, NOT 2 s (doubled)
  REQUIRE(sg_host_get_carbon_footprint(host) == Approx(0.027778).margin(0.000001));
}
