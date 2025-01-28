# EVerest Features and Roadmap
as of 2025-01-27

## Done:
EVerest grows constantly and besides the BelayBox development kit from PIONIX,
it has been used as a charging device firmware by several charging device
manufacturers.

We are currently incorporating the following standards and technologies:

### Communication protocols & standards

* ISO 15118 - current status, see https://github.com/everest/libiso15118
  Some examplary implemented features:
  + -20: DC, DC BPT, Scheduled Mode and Dynamic Mode
  + ISO 15118-2 AC charging
    (preliminary external java dependencies, will be replaced) and DC charging
  + Multiplexing of DIN, ISO 15118-2 and ISO 15118-20
  + SLAC / ISO 15118-3 in C++
  + Plug&Charge handling
* C++ based EXI parser & parser generator (ISO 15118-20 capable)
* OCPP
  + OCPP 1.6+ (JSON) - All profiles and security extensions
  + OCPP 2.0.1 field-relevant implementation done, e.g.:
    + Device Model implementation
    + Remote Start / Stop
    + Authorization Cache
    + FirmwareUpdate
    + California Pricing requirements
    + Smart Charging (not supported: Use cases K11-K17)
    + and many more, see https://github.com/everest/libocpp for details.
* DC DIN SPEC 70121
* EN 61851
* SAE J2847/2 DC BPT, V2H & V2G, Negative PhysicalValueTypes in
  ChargeParameterDiscovery and CurrentDemand messages.
* Modbus
* Sunspec

### Hardware & software interfaces

* Hardware abstraction layer
* EVerest core interconnecting all charging protocols and interfaces.
* NFC authentication

### Software architecture & development

* MQTT framework to easily configure loosely coupled modules
* Software in the loop simulation
* Testing framework
* Error handling and reporting features
* NodeRed integration


## Work in Progress:

### Milestone Q1/2025
* ISO 15118-20
  + Pause / Resume
  + V2X DC Setpoint
  + "Qualification" status of 15118-20 branch
* OCPP 2.1
  + Core
  + ISO 15118-20 updates
* EEBus-GO integration
  + Basic
  + Add support for Setpoint / Frequency Control
  + Add external EMS API module
* Drivers
  + BSP Chargebyte hardware (eventually)
  + InfyPower power module

### Milestone Q2/2025
* EvseManager refactoring
* GB/T integration
* OCPP/ISO 15118-20 data sharing
* OCPP 2.1
  + V2X Setpoint
  + Resume Transactions
  + EMS Support
* EEBus-GO: Further use cases
* OpenADR 3.0: OpenLEADR-rs integration
* Driver: UUGreen power module

### Milestone Q4/2025
* ISO 15118
  + -2: Certificate update
  + -2: Smart Charging
  + -10: SLAC refactoring
  + -20Amd - AC V2X
  + -20: V2X DC Frequency Control
* MCS
  + Low Level Communication
  + High Level Communication
* OCPP: Minimum Required Error Codes (MREC)
* OCPP 2.1
  + V2X Frequency Control
  + Priority Charging
  + V2X DER
* Matter: EV charging profiles

### Milestone 2026
* ISO 15118
  + -20: Plug&Charge
  + -20: DER
  + -202: ESDP
  + libISO15118: EV-side simulator
* OCPP 2.0.1: ISO 15118 Smart Charging
* OCPP 2.1
  + Ad-hoc Payments
  + Cost Calculation
  + Pre-paid Card
  + Event Streams

## Non-prioritized list of further ideas / features

Our vision is that EVerest enables every way of (at least a bit) smart
charging, in all situations, from home to Work and even Public AC and DC
unidirectional and bidirectional grid friendly charging.
Moving forward, we have quite a list of things we want to add, and this is
probably far from complete:

* Further OCPP 2.0.1 / 2.1 implementation
  + Event Battery Swapping
  + Server-client (local energy management in the middle)
* Further ISO 15118-20 implementation
  + Also ISO 15118-8: WiFi communication
* Smart charging: based on solar generation and dynamic load balancing
* IEEE 2030.5
* Car Communication
  + CHAdeMO
  + ChaoJi
* Advanced Energy Management with load balancing, solar integration and
  dynamic pricing
* Smart Home Integration:
  + Further EEBus implementation (https://www.eebus.org/)
  + SG Ready
  + Smart Meter Integration
* Remote System Architecture
* Payment APIs
* Further backend integrations: https://www.openchargealliance.org/
* Grid integration
  + ADR https://www.openadr.org/
  + USEF https://www.usef.energy/
* More HW drivers for e.g. Meters, other AC and DC charging controllers
* Ongoing
  + Car compatibility tests with various OEMs
  + New drivers for components
