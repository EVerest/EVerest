# EVerest Features and Roadmap
as of 2023-08-04

## Done:
EVerest grows constantly and besides the BelayBox development kit from Pionix,
it has been used as a charging device firmware by several charging device
manufacturers.

We are currently incorporating the following standards and technologies:

* EVerest core interconnecting all charging protocols and interfaces.
* MQTT framework to easily configure loosely coupled modules
* Hardware abstraction layer
* Software in the loop simulation
* ISO 15118 (AC wired charging)
  + SLAC / ISO 15118-3 in C++
  + ISO 15118-2 AC charging
    (preliminary external java dependencies, will be replaced) and DC charging
  + Plug&Charge handling
* EN 61851
* DC DIN SPEC 70121
* OCPP 1.6+ (JSON) - All profiles and security extensions
* OCPP 2.0.1 partly, already done:
  + Device Model implementation
  + Remote Start / Stop
  + Authorization Cache
  + FirmwareUpdate
  + and many more
* Smart charging: based on energy prices and limiting based on grid constraints 
  + Price API form: aWATTar, Tibber
  + Solar forecast API: https://forecast.solar/
* Modbus
* Sunspec
* NFC authentication
* NodeRed integration
* Testing framework

## Work in Progress:
* Further OCPP 2.0.1 / 2.1 implementation
* Ongoing ISO 15118-20 implementation (already done: Fully tested and
  operational ISO 15118-2 / DIN SPEC 70121 implementation, bidirectional PoC
  based on C/C++ implementation by Chargebyte/Pionix)
* Smart charging: based on solar generation and dynamic load balancing
* Remote System Architecture
* Error Handling and Reporting
* CarSimulator with ISO 15118-20 AC
* ISO 15118-X - all other features (DC, Wireless, Bidirectional, Plug&Charge)
* C++ based EXI parser & parser generator (ISO 15118-20 capable)
* Ongoing
  + Car compatibility tests with various OEMs
  + New drivers for components

## Planned:
Our vision is that EVerest enables every way of (at least a bit) smart charging, in all situations, from home to Work and even Public AC and DC unidirectional and bidirectional grid friendly charging. Moving forward, we have quite a list of things we want to add, and this is probably far from complete:
* Further backend integrations: https://www.openchargealliance.org/
* Car Communication
  + CHAdeMO
  + GB/T
  + ChaoJi
* Advanced Energy Management with load balancing, solar integration and
  dynamic pricing
* Grid integration
  + ADR https://www.openadr.org/ 
  + USEF https://www.usef.energy/
* Smart Home Integration:
  + EEBus https://www.eebus.org/ 
  + SG Ready
  + Smart Meter Integration
* IEEE 2030.5
* More HW drivers for e.g. Meters, other AC and DC charging controllers
* Payment APIs
