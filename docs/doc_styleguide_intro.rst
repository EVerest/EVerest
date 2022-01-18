.. doc_styleguide_intro:

Styleguide
############
*Current status of Q1 2021*

Namespacing
************


Take the most advantage of the interfaces of the Framework
*************************************************************

No Stringchains
================

Stringchains to tranfere commulated informations as a variable are to aviod. This kind of communications corrupt the informationstyle

**Wrong**

.. code-block:: python

    self.power_config = "Solar#23A#3phase#EcoA"

**Better**

Dictionaries are possible to send bounded informations with clearly understandable keys and can be eximaned by the restrictions in the manifest


entpacken von config am vorteil bei sehr tiefschichtigen configs


Try to reduce variables when possbile
=====================================

**Try to integragte functions directly like:**

.. code-block:: python

    if len(car_list) > 10:
        print ("Maximum number of cars for charging is reached")


**Use dictionaries instead of many diverse suffixes:**

Instead of

.. code-block:: python

    self.car_config_user_renault_zoe_power_min = 41
    self.car_config_user_renault_zoe_power_max = 52
    self.car_config_user_renault_zoe_battery_voltage = 400

use

.. code-block:: python

    self.car_config_user = {"name": "Renault Zoe", "power_min": 41, "power_max": 52, "battery_voltage": 400}

Don't use different types for one variable. 

.. code-block:: python

    if len(power_list) <= 0:
        self.max_current = "off"
    else:
        self.max_current = max_current

It is possible, but the json-code is cleaner if there is only one type for one variable.
At example, when in the manifest *max_current* is defined as number, in the unit-script it should be used only as float.

.. code-block:: python

    if len(power_list) <= 0:
        self.max_current = 0.0
    else:
        self.max_current = max_current

**Hier sollte noch gezeigt werden wie mehrere typen sonst aussehen**










