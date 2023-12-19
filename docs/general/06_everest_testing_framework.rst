.. everest_testing_framework:

.. _testing_framework_main:

#############################
The EVerest testing framework
#############################

********
Overview
********
TODO: Give an overview of the EVerest testing framework:
Based on pytest, running a full instance of EVerest with actual modules, etc.

************
Installation
************
TODO: How to install everestpy (framework) and everest-testing (utils), and
other prerequisites (Python packages, pytest extensions, etc.)

*************
Pytest basics
*************
TODO: Explain the concepts of: fixtures, markers, conftest, test files/test methods
and naming conventions ("test_" prefix). Provide links to pytest docs where appropriate

******************************************
Example: Running a SIL charging simulation
******************************************
TODO: Show an example test that runs the SIL simulation

************************
The (magic) probe module
************************
TODO: Explain how the probe module works and how to use it

TODO: Explain what the magic probe module is and how to use it

*****************
Debugging modules
*****************
TODO: Explain how to run a module under a debugger while testing
(make it standalone, start it under a debugger with the temp config file path)

*****************************
Reference: Important fixtures
*****************************
TODO: Explain the most commonly used fixtures in everest-testing core-utils and ocpp-utils

****************************
Reference: Important markers
****************************
TODO: Explain the most commonly used markers in everest-testing core-utils and ocpp-utils

*******************
Overriding fixtures
*******************
TODO: (briefly) explain how fixture overriding works in pytest, and give examples for why
it is useful

****************************
Config adjustment strategies
****************************
TODO: Explain what config adjustment strategies are and how everest-testing adjusts the
runtime config step by step. Explain how to create and add your own config adjustments.

******************************
OCPP testing: Mocking the CSMS
******************************
TODO: Explain how to use the ChargePoint from everest-testing to mock an OCPP CSMS

**********************
Testing best practices
**********************
TODO: List some best practices when testing, e.g. making sure that tests only use
temporary files, that they can run in parallel with other tests, etc.
