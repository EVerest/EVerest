.. _everest_modules_handwritten_TemperatureSensorSimulator:

.. #############################
.. TemperatureSensorSimulator
.. #############################

External MQTT Control
=====================

The TemperatureSensorSimulator publishes ``temperature_sensor`` readings on a configurable
interval. Values can be changed at runtime via MQTT. Topic names are logged when the module
starts (``module_id`` is the instance id from the Everest config).

.. code-block:: none

    everest_api/<module_id>/cmd/set_temperature_C
    everest_api/<module_id>/cmd/set_identification
    everest_api/<module_id>/cmd/set_location
    everest_api/<module_id>/cmd/set_publish_interval_ms
    everest_api/<module_id>/cmd/stop_publishing
    everest_api/<module_id>/cmd/start_publishing

Example (SIL config ``config/config-sil-ac-temp-derating.yaml``, module id ``temp_sensor_sim``)::

    mosquitto_pub -t everest_api/temp_sensor_sim/cmd/set_temperature_C -m 55
    mosquitto_pub -t everest_api/temp_sensor_sim/cmd/stop_publishing -m ""
    mosquitto_pub -t everest_api/temp_sensor_sim/cmd/start_publishing -m ""

See also :ref:`AcTemperatureDerating <everest_modules_handwritten_AcTemperatureDerating>` and
``config/config-sil-ac-temp-derating.yaml``.
