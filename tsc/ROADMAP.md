# EVerest Roadmap
as of 2021-11-03
## Work in progress:
EVerest is currently running only with a dedicated HW setup of Pionix, aiming to solve two minimum viable use cases:  
1. Smart charging at home
2. Public AC charging with integration of std. OCPP backend

To enable this, we are currently incorporating the following standards and technologies.
* ISO 15118 (AC wired charging)
* EN 61851
* OCPP 1.6 (JSON) - Core Profile
* Modbus
* Sunspec
* MQTT framework to easily configure loosely coupled modules
* NFC authentication
* NodeRed integration
* Packetization
* Smart Charging based on energy prices and solar production
* Price API form: aWATTar, Tibber
* Solar forecast API: https://forecast.solar/ 

## Planned:
Our vision is that EVerest enables every way of (at least a bit) smart charging, in all situations, from home to Work and even Public AC and DC unidirectional and bidirectional grid friendly charging. Moving forward, we have quite a list of things we want to add, and this is probably far from complete:
* Prio 1: Web User Interface
  + For Configuration (Factory, Installation, User)
  + For just end users
  + For display on optional embedded (touch) screens
* Prio 2: Backend Integration: https://www.openchargealliance.org/
  + OCPP 1.6 - all optional profiles
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

