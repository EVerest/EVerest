How To: Develop New Modules
***************************

Introduction
__________________________
In this example we use Visual Basic Code, but every other programming editor is equivalent to develop inside the everest framework.
The best beginning to create new packages is to create them directly inside the project and understand the basics and rules to create a stable package.
Working with the github repository is the safest solution to get the acual version of the framework and all the functionalities of the EVerst Charging Station.
For a fluid workaround it is recommended adding an ssh-key, like its discribed inside the github documentation https://docs.github.com/en/github/authenticating-to-github/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent.
With an ssh-key you do not need to reenter your credentials for every pull of your infranet workspace.


After understanding the concept of the framework, descirbed in the following documentation:**Here comes a link**
, you can start to create your own new additions or recrate the existing modules/units with your own implementations. 

Don't forget by recreating modules to provide the required signals to other modules, which is possible to check by comparing your outcomes 
with the classdiagram of the basic enviroment: **Here comes a link**

As an example a new module called **PowerLaboratory** will be now generated in the modules-directory.


1. New directory
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
With a right-click on the modules-directory there is the option "New Folder". With this it is needed to create a folder with the name of your module 
written in camelcase.


2. Creating the essential files
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
With right-click on your new module-folder you have to create new files as the essential components
The __init__.py, the manifest.json and the unit-script written in python


4. Define the __init__.py
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The __init__.py declare the Class, also named with camelcase as ModuleMain which is required to start the module through the command line.

.. code-block:: python
    :linenos:

    from utils import LogManager
    logger = LogManager.get_logger(__name__)
    del LogManager
    from .power_laboratory import PowerLaboratory as ModuleMain


5. Define the manifest.json
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The manifest of the module declares the units with unit_ids. Because the modules only provides one module and it was declared as the main module, 
the name of the unit is simply defined as "main" which provides the class which will be declared in the python script. As an additional requirement, 
the licence and the name of the author should be a part of the manifest.

.. code-block:: javascript
    :linenos:
    
    {
        "provides": {
            "main": {
                "class": "power_laboratory"
            }
        },
        "metadata": {
            "license": "https://opensource.org/licenses/MIT",
            "author": "Your Name"
        }
    }

If the module requires connections to variables or commands to other unit-classes then its is necessary to define these requirements with a suitable
lowcased requirement-name. 
    
.. code-block:: json
    :linenos:

    {
        "provides": {
            "main": {
                "class": "power_laboratory"
            }
        },
        "requires": {
            "reqpower": {
                "class": "Power"
            }
        },
        "metadata": {
            "license": "https://opensource.org/licenses/MIT",
            "author": "Hubert Padusinski"
        }
    }        

The main unit can provide different configurations and interfaces as variables and commands to other units. For more structure inside the EVerest-enviroment, 
the order should be followed in as "config", "vars" and then "commands".Configs can be declared as internal inital constants or a list with propterties for 
a specific unit. They can be embedded inside the python script and in the manifest contains the restrictions. Restrictions are mimimal declared by the type of 
the object and typically can includes the allowed characters in id's, names or messages, minmum, maximum values. An overview of options gives the
**Here comes a link:framework-library**

It is optionally possible to set a default value for the config, but usually all configuration values have to be declared inside the config.json.
Providing Variables as "vars" are described by keeping the style of the configs, but are not declared in the config.json. 
A command can be empty or bring along input keys. The commands as "cmds" are declared in the manifest with the possiblity to contain restricted 
arguments by type or value limits and also declare the rules of an expected result.

.. code-block:: json
    :linenos:

    {
        "provides": {
            "main": {
                "class": "power_laboratory",
                "config": {
                    "max_current":{
                        "description": "Provides maximum current of this supply in ampere",
                        "type": "number",
                        "minumum": 1,
                        "maximum": 60
                    },
                },
                "vars": {
                    "max_current":{
                        "description": "Provides maximum current of this supply in ampere",
                        "type": "number"
                    },
                }
                "cmds": {
                    "start_power_laboratory": {
                        "description": "This command starts the power ",
                        "arguments": {
                            "start_current": {
                                "description": "This argument specifies to charge with the current at start",
                                "type": "number",
                                "minimum": 0,
                                "maximum": 60
                            },
                        },
                        "result": {
                            "type": "string"
                        }
                    }
                }
            },
            "requires": {
                "reqpower": {
                    "class": "Power"
                }
            }
        },
        "metadata": {
            "license": "https://opensource.org/licenses/MIT",
            "author": "Thilo Molitor"
        }
    }

The *power_laboratory* class shows examples for the configurations and interfaces.Connections between units can have the same namespacings for configurations and variables,
due to the use of IDs and separate functions, which makes the identification of the interface type self-explanatory.

    **max_current** as constant

    As a constant of the unit power_laboratory is minimal defined with the type as number and optionally declared with minimum and maximum values. 
    If the vaule in the config is outside the allowed range, the framework will response with an error by starting the enviroment.
    The discription allows more intelligibility to understand the sense behind a configuration, but it is optional in use.
    Other unit-scripts can also import the configuration of the *power_laboratory* class.

    **max_current** as variable

    As a variable the unit-script of power_laboratory is able to publish it with an publish-function *everest.publish_var(unit_id, var_name, value)* 
    from the everest framework. The framework checks by publishing the type or restrictions and raises if necessary an error.

    **start_current** as a command

    A unit can provide commands with or without input-keys and optionally results, which also can be defined in types, restricitons, limits or default values.
    Inside the unit-script power_laboratory the command needs to be functionally defined and describe the reaction,
    through the *everest.provide_cmd(unit_id, cmd_name)*-function. Another unit can trigger the command by using *everest.call_cmd(requirement_id, cmd_name, args)*.






5. Creating a unit-script
^^^^^^^^^^^^^^^^^^^^^^^^^^

Functionalities are created in python-scripts with a minimal content.
At the beginning the required libraries are imported from async, the utlitys and the framework.
Inside the unit-script, the class with camelcase need to be introduced with minimum the *__init__-object*, where typically the configs are imported.
Should a unit class begin with starting a routine, then this is realizable by waiting for a *everest.ready*- signal.

Like it was indicated in the description of the manifest, a variable can be published by using the publish_var-function. In the example of PowerLaboratory,
the max_current is published with the unit_id and the var_name, which they have to be synchronous to the definition in the manifest. In this example the config value
is imported as a global variable and is directly published.


.. code-block:: python
    :linenos:

    # import python libs
    import asyncio

    # import needed parts of our framework
    from . import logger
    from framework import everest, task, time

    class PowerLaboratory(object):
        def __init__(self, config):
            logger.info("Got config: %s" % str(config))
            self.max_current = config["main"]["max_current"]

        @everest.ready
        def ready(self):
            everest.publish_var(unit_id="main", var_name="max_current", value=self.max_current)

Exercise
""""""""""""""""""""""""
**A** 
*Now you can try to rebuild an new module with essential-scripts in orientation to the PowerLaboratory-Module, 
but rename it with a PowerSolar respectively power_solar namespacing.*

**B**
*As an example a simple third module called **Power** should be created with its essential scripts, but it has no configs or commands. 
It only provides the variable max_current with the same type and restrictions.
In additon it requires PowerLaboratory and PowerSolar.
The python-script is empty*
    

5. Connecting Modules
^^^^^^^^^^^^^^^^^^^^^^^^^^
Now you should have three modules, two that provides a value and the third will now listen on both others.
The script of the Power-unit shows that at the beginning the Power-module just initiate its globale variable of self.max_current and is subscribing the variables of
the PowerLaboratory and PowerSolar modules.
If each of this modules publish a new varable they are saved in the self.max_current-dictionary with an individual key.
Directly the routine jumps to the incoming_current-object, where the the resulting max_current of the power-module is published. Resulting means the minimum value 
of the actual registered max_current-values of each connected modules.
The module PowerSolar has a special relation to the PowerModule, because it is declared as *optional*. This configuration will be introduced in the config.json,
but for generating more selfcontrol and a better understanding of the code, the framework demands the key "optional:", whenever this module is subscribed.
A missing optional-key results an error.


.. code-block:: python
    :linenos:

    # import python stuff
    import asyncio

    # import needed parts of our framework
    from . import logger
    from framework import everest, task, time

    class Power(object):
        def __init__(self, config):
        self.max_current = {}

        @everest.subscribe_var(requirement_id="powerin", var_name="max_current")
        def incoming_current_powerin(self, max_current:float):
            self.max_current["powerin"] = max_current
            logger.debug("Incoming power_in current: %s" % str(self.max_current))
            self.incoming_current()

        @everest.subscribe_var(requirement_id="optional:solar", var_name="max_current")
        def incoming_current_solar(self, max_current:float):
            self.max_current["optional:solar"] = max_current
            logger.debug("Incoming solar current: %s" % str(self.max_current))
            self.incoming_current()
            
        def incoming_current(self):
            everest.publish_var(unit_id="main", var_name="max_current", value=min(self.max_current.values()))



To establish the connection in the enviroment and set the configurations all modules have to be declared and defined in the config.json in the /everst-main-directory.
Configs are part of the unit and gets a valid value, like here for the max_current-configs of the main units of each PowerLaboratory and PowerSolar-modules.
Connections are fullfiled by describing which unit fullfills a requirement an explicit module inside its manifest.

.. code-block:: javascript
    :linenos:

    }
        }
        "powerlab": {										
            "module": "PowerLaboratory",							
            "config": {
                "main": {
                    "max_current": 16,
                }
            }
        },
        "power": {										
            "module": "Power",	
            "connections": {
                "powerin": {
                    "module_id": "powerlab",
                    "unit_id": "main"
                },
                "optional:solar": {
                    "module_id": "solar",
                    "unit_id": "main"
                }
            }						
        },
        "solar": {										
            "module": "PowerSolar",	
            "config": {
                "main": {
                    "max_current": 16,
                }
            }						
        }
    }
