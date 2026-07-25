# Validation of the SimGrid Environmental Footprint Plugin (v2)

This document describes how we validated our plugin to compute the **operational carbon emissions** and **operational water consumption** from electricity usage of machines in SimGrid. The plugin exposes three operational components and two fixed inventory values:

1. **Off-site footprint** — emissions and water used at power plants to generate electricity, scaled by PUE
2. **On-site water consumption** — water used for datacenter cooling (WUE)
3. **Embodied footprint** — fixed carbon and water values associated with hardware manufacturing, reported separately from operational totals

We consider static and dynamic values for grid electricity emissions (to represent the variability caused by using intermittent renewable sources), and we validate for the following scenarios:

- Machines off
- Machines idle
- Performing computations

---

## Inputs

### Hardware Properties

We use homogeneous machines with the following power profile:

| CPU cores used | Power (W) |
| --- | --- |
| 1 core | 125 W |
| 2 cores | 150 W |
| 3 cores | 175 W |
| 4 cores | 200 W |
| State | Power (W) |
| --- | --- |
| Idle | 100 W |
| Off | 5 W |

**Note:** Our plugin extends SimGrid's energy plugin, which has been validated on both homogeneous and heterogeneous platforms. Although the scenarios below consider only homogeneous machines, the plugin also supports heterogeneous machines.

### Datacenter Parameters

| Parameter | Value | Description |
| --- | --- | --- |
| **PUE** | 1.2 | Power Usage Effectiveness — ratio of total facility energy to IT equipment energy |
| **WUE** | 1.8 L/kWh | Water Usage Effectiveness — on-site cooling water per kWh of IT energy |
| **Embodied Carbon** | 1,000,000 gCO₂ | Total CO₂ embedded in host manufacturing |
| **Embodied Water** | 50,000 L | Total water embedded in host manufacturing |

### Model Equations

For each simulation time step *dt*:

- $E_{IT}$ = energy consumed by the host (from SimGrid's energy plugin)
- $E_{total} = E_{IT} \times PUE$ (total datacenter energy including cooling overhead)
- $CI$ = Weighted carbon intensity of the energy mix, in $gCO_{2}eq/kWh$
- $\text{Carbon}_{offsite} = E_{total} \times CI$ (CO₂ from power generation)
- $WI$ = Weighted water intensity of the energy mix, in $L/kWh$
- $\text{Water}_{onsite} = E_{IT} \times WUE$ (cooling water)
- $\text{Water}_{offsite} = E_{total} \times WI$ (water used in power generation)

The embodied values are fixed host inventory:

- $\text{Embodied}_{carbon} = \text{embodied\_carbon}$
- $\text{Embodied}_{water} = \text{embodied\_water}$

They do not depend on *dt* and are not included in the operational carbon and water totals.

---

## Carbon-Intensity & Water-Intensity Data

We consider three grid locations, using data from [Electricity Maps](https://www.electricitymaps.com/):

| Location | CI (gCO₂/kWh) | WI (L/kWh) | Characteristics |
| --- | --- | --- | --- |
| **USA** | 390.0231 | 1.8888 | Higher carbon intensity, low renewables |
| **France** | 28.3792 | 3.7402 | Lower carbon intensity (nuclear) |
| **Brazil** | 69.1152 | 11.8376 | Intermediate (hydroelectric) |

### Fixed Embodied Footprint

The example platform stores the following fixed values for every host:

- $\text{Embodied carbon} = \textbf{1{,}000{,}000 gCO₂}$
- $\text{Embodied water} = \textbf{50{,}000 L}$

These values are available through their dedicated getters and setters. The plugin does not amortize, allocate, or add them to the operational results below.

---

# Validation Scenarios

All scenarios use a **10-hour time horizon**.

---

# 1. Static Scenarios

## 1.1 Machine Off (USA grid)

**Parameters:** Power = 5 W, CI = 390.0231 gCO₂/kWh, WI = 1.8888 L/kWh

- $E_{IT} = 5 \text{ W} \times 10 \text{ h} = 50 \text{ Wh} = 0.05 \text{ kWh}$
- $E_{total} = 0.05 \times 1.2 = 0.06 \text{ kWh}$

| Component | Formula | Value |
| --- | --- | --- |
| Carbon (off-site) | 0.06 × 390.0231 | **23.4014 g** |
| Water (on-site) | 0.05 × 1.8 | **0.0900 L** |
| Water (off-site) | 0.06 × 1.8888 | **0.1133 L** |
| **Operational Carbon** |  | **23.4014 g** |
| **Operational Water** |  | **0.2033 L** |

---

## 1.2 Machine Idle (France grid)

**Parameters:** Power = 100 W, CI = 28.3792 gCO₂/kWh, WI = 3.7402 L/kWh

- $E_{IT} = 100 \text{ W} \times 10 \text{ h} = 1.0 \text{ kWh}$
- $E_{total} = 1.0 \times 1.2 = 1.2 \text{ kWh}$

| Component | Formula | Value |
| --- | --- | --- |
| Carbon (off-site) | 1.2 × 28.3792 | **34.0550 g** |
| Water (on-site) | 1.0 × 1.8 | **1.8000 L** |
| Water (off-site) | 1.2 × 3.7402 | **4.4882 L** |
| **Operational Carbon** |  | **34.0550 g** |
| **Operational Water** |  | **6.2882 L** |

---

## 1.3 Machine Running Tasks (Brazil grid)

**Parameters:** CI = 69.1152 gCO₂/kWh, WI = 11.8376 L/kWh

We evaluate 4 sequential task segments:

- A: 1 CPU core for 1 hour
- B: 2 CPU cores for 2 hours
- C: 3 CPU cores for 3 hours
- D: 4 CPU cores for 4 hours

| Scenario | Cores | Power (W) | Duration (h) | E_IT (kWh) | E_total (kWh) | CO₂ off-site (g) | H₂O on-site (L) | H₂O off-site (L) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 1 | 125 | 1 | 0.125 | 0.150 | 10.367 | 0.225 | 1.776 |
| B | 2 | 150 | 2 | 0.300 | 0.360 | 24.882 | 0.540 | 4.262 |
| C | 3 | 175 | 3 | 0.525 | 0.630 | 43.543 | 0.945 | 7.458 |
| D | 4 | 200 | 4 | 0.800 | 0.960 | 66.351 | 1.440 | 11.364 |
| **Total** | — | — | 10 | **1.750** | **2.100** | **145.142** | **3.150** | **24.859** |

**Operational totals:**

| Component | Value |
| --- | --- |
| CO₂ off-site | 145.14 g |
| **Operational CO₂** | **145.14 g** |
| H₂O on-site | 3.15 L |
| H₂O off-site | 24.86 L |
| **Operational H₂O** | **28.01 L** |

---

# 2. Dynamic Scenarios

In these scenarios, the carbon intensity varies hourly. WUE and PUE remain constant.

---

## 2.1 Machine Off — Hourly CI from USA

Power = 5 W → $E_{IT}/h = 0.005$ kWh → $E_{total}/h = 0.006$ kWh

| Hour | CI (gCO₂/kWh) |
| --- | --- |
| 1 | 453.54 |
| 2 | 441.48 |
| 3 | 437.93 |
| 4 | 437.61 |
| 5 | 442.29 |
| 6 | 447.18 |
| 7 | 452.04 |
| 8 | 453.96 |
| 9 | 455.13 |
| 10 | 457.54 |
| **Subtotal** | — |
| Component | Value |
| --- | --- |
| CO₂ off-site | 26.87 g |
| **Operational CO₂** | **26.87 g** |
| H₂O on-site | 0.09 L |
| H₂O off-site | 0.11 L |
| **Operational H₂O** | **0.20 L** |

---

## 2.2 Machine Idle — Hourly CI from France

Power = 100 W → $E_{IT}/h = 0.1$ kWh → $E_{total}/h = 0.12$ kWh

| Hour | CI (gCO₂/kWh) |
| --- | --- |
| 1 | 29.09 |
| 2 | 30.08 |
| 3 | 32.33 |
| 4 | 32.96 |
| 5 | 33.00 |
| 6 | 33.41 |
| 7 | 34.52 |
| 8 | 33.63 |
| 9 | 32.17 |
| 10 | 31.95 |
| **Subtotal** | — |
| Component | Value |
| --- | --- |
| CO₂ off-site | 38.78 g |
| **Operational CO₂** | **38.78 g** |
| H₂O on-site | 1.80 L |
| H₂O off-site | 4.49 L |
| **Operational H₂O** | **6.29 L** |

---

## 2.3 Machine Running Tasks — Hourly CI from Brazil

Power consumption varies by active core count (same as Section 1.3).

### Hourly Carbon Intensities

| Hour | CI (gCO₂/kWh) |
| --- | --- |
| 1 | 100.07 |
| 2 | 93.60 |
| 3 | 93.89 |
| 4 | 96.04 |
| 5 | 95.00 |
| 6 | 94.40 |
| 7 | 94.11 |
| 8 | 94.99 |
| 9 | 96.44 |
| 10 | 99.76 |

### Energy & Emissions by Task Segment

| Scenario | Cores | Hours | Power (W) | E_IT/h (kWh) | E_total/h (kWh) | CO₂ off-site (g) | H₂O on-site (L) | H₂O off-site (L) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 1 | 1 | 125 | 0.125 | 0.150 | 0.15 × 100.07 = **15.011** | 0.225 | 1.776 |
| B | 2 | 2–3 | 150 | 0.150 | 0.180 | 0.18 × (93.60 + 93.89) = **33.748** | 0.540 | 4.262 |
| C | 3 | 4–6 | 175 | 0.175 | 0.210 | 0.21 × (96.04 + 95.00 + 94.40) = **59.942** | 0.945 | 7.458 |
| D | 4 | 7–10 | 200 | 0.200 | 0.240 | 0.24 × (94.11 + 94.99 + 96.44 + 99.76) = **92.472** | 1.440 | 11.364 |
| **Subtotal** | — | 10 h | — | **1.750** | **2.100** | **201.173** | **3.150** | **24.859** |

### Final Operational Totals

| Component | Value |
| --- | --- |
| CO₂ off-site | 201.17 g |
| **Operational CO₂** | **201.17 g** |
| H₂O on-site | 3.15 L |
| H₂O off-site | 24.86 L |
| **Operational H₂O** | **28.01 L** |

---

# Summary: Impact of the Operational Model

The table below compares the original model (off-site only, no PUE) with the operational model including PUE and on-site water:

| Scenario | CO₂ (original) | CO₂ (operational) | Δ CO₂ | H₂O (original) | H₂O (operational) | Δ H₂O |
| --- | --- | --- | --- | --- | --- | --- |
| 1.1 Off (USA, static) | 19.50 g | 23.40 g | +20% | 0.09 L | 0.20 L | +126% |
| 1.2 Idle (France, static) | 28.38 g | 34.06 g | +20% | 3.74 L | 6.29 L | +68% |
| 1.3 Tasks (Brazil, static) | 120.95 g | 145.14 g | +20% | 20.72 L | 28.01 L | +35% |
| 2.1 Off (USA, dynamic) | 22.39 g | 26.87 g | +20% | — | 0.20 L | — |
| 2.2 Idle (France, dynamic) | 32.31 g | 38.78 g | +20% | — | 6.29 L | — |
| 2.3 Tasks (Brazil, dynamic) | 167.64 g | 201.17 g | +20% | — | 28.01 L | — |

<aside>
💡

**Key insight:** PUE increases off-site operational impacts in direct proportion to facility overhead, while WUE adds a separate on-site water component. Fixed embodied inventory is intentionally excluded from this comparison.

</aside>

---

# Notes

- **PUE** is treated as constant (1.2) across all scenarios. In practice, PUE can vary with ambient temperature.
- **WUE** is treated as constant (1.8 L/kWh) here, but the plugin supports dynamic updates via `sg_host_set_wue()` to model variation with wet-bulb temperature.
- **Embodied values** (1 tonne CO₂, 50,000 L) are illustrative fixed inventory values. Real values should be sourced from manufacturer LCA reports or frameworks like iMec, and any allocation should be performed outside the plugin.
- **Water intensity** (WI) represents the *off-site* water footprint of power generation (EWIF in BluePulse terminology), not cooling water.
- The dynamic scenarios (Section 2) currently only vary carbon intensity hourly. A more complete validation could also vary WUE hourly based on meteorological traces.
