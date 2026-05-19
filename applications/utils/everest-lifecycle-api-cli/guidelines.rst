EVerest Lifecycle API CLI Guidelines
=====================================

This CLI tool implements the EVerest Lifecycle API as defined in
`EVerest/docs/source/reference/EVerest_API/lifecycle_API.yaml`.

The Lifecycle API provides commands to control the lifecycle of EVerest modules:
- **stop_modules**: Request EVerest to stop all currently running modules
- **start_modules**: Request EVerest to start modules
- **monitor**: Subscribe to and display lifecycle status updates

Usage
-----

Basic command format::

    everest-lifecycle-api-cli [options] <command> [args...]

Global Options
--------------

- ``--host <hostname>``: MQTT broker host (default: localhost)
- ``--port <port>``: MQTT broker port (default: 1883)
- ``--api-name <name>``: API name for MQTT topic formation (default: lifecycle)
- ``--help, -h``: Show help message

Commands
--------

**stop_modules**

Stop all currently running modules::

    everest-lifecycle-api-cli stop_modules

**start_modules**

Start modules based on the active configuration slot::

    everest-lifecycle-api-cli start_modules

**monitor**

Monitor lifecycle status changes in real-time::

    everest-lifecycle-api-cli monitor

This command displays status updates including module execution state and
configuration service availability.

Examples
--------

Stop all modules on remote broker::

    everest-lifecycle-api-cli --host 192.168.1.100 --port 1883 stop_modules

Monitor local lifecycle status::

    everest-lifecycle-api-cli monitor

Implementation Notes
--------------------

The CLI communicates with EVerest via MQTT using the Lifecycle API topics:

- Request topics: ``everest_api/1/lifecycle/m2e/{operation}``
- Reply topics: Custom reply topic specified in request headers
- Status topic: ``everest_api/1/lifecycle/e2m/status``

Responses include result enums indicating operation status:

- **StopModulesResultEnum**: Stopping | NoModulesToStop | Rejected
- **StartModulesResultEnum**: Starting | Restarting | NoConfigToStart | Rejected
- **ExecutionStatusUpdateNotice**: Module status and configuration service availability
