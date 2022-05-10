# EVerest Features and Roadmap
as of 2022-05-10

## Done (Mainly):
EVerest is currently running only with a dedicated HW setup of Pionix, aiming to solve two minimum viable use cases:  
1. Smart charging at home
2. Public AC charging with integration of std. OCPP backend

To enable this, we are currently incorporating the following standards and technologies.
* EVerest core interconnecting all charging protocols and interfaces.
* MQTT framework to easily configure loosely coupled modules
* Hardware abstraction layer
* Software in the loop simulation
* ISO 15118 (AC wired charging)
  + SLAC / ISO 15118-3 in C++ (done)
  + ISO 15118-2 AC charging (preliminary external java dependencies, will be replaced)
* EN 61851
* OCPP 1.6+ (JSON) - All profiles and security extensions
* Modbus
* Sunspec
* NFC authentication
* NodeRed integration
* Smart charging: based on energy prices and limiting based on grid constraints 
  + Price API form: aWATTar, Tibber
  + Solar forecast API: https://forecast.solar/ 

## Work in Progress:
* Packetization
* OTA update support
* Smart charging: based on solar generation and dynamic load balancing 
* Web User Interface
  + For Configuration (Factory, Installation, User)
  + For just end users
  + For display on optional embedded (touch) screens

## Planned:
Our vision is that EVerest enables every way of (at least a bit) smart charging, in all situations, from home to Work and even Public AC and DC unidirectional and bidirectional grid friendly charging. Moving forward, we have quite a list of things we want to add, and this is probably far from complete:
* Further backend integrations: https://www.openchargealliance.org/
  + OCPP 2.0.1
* Car Compatibility
  + Test with various OEMs
* Car Communication
  + ISO 15118-X - all other features (DC, Wireless, Bidirectional, Plug&Charge)
  + DC DIN SPEC 70121
  + CHAdeMO
* Grid integration
  + ADR https://www.openadr.org/ 
  + USEF https://www.usef.energy/
* Smart Home Integration:
  + EEBus https://www.eebus.org/ 
  + SG Ready
  + Smart Meter Integration
* More HW drivers for e.g. Meters, other AC and DC charging controllers
* Payment APIs
