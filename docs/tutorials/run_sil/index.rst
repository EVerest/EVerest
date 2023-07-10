.. doc_sil

Tutorial: Simulate EVerest in Software
######################################

You can create custom simulations with your very own structure of modules and
data flows.

.. hint:: 
  
  How to set up and run an already pre-configured simulation in
  EVerest can be found in the Quick Start Guide section
  `Simulating EVerest <../../general/02_quick_start_guide.html#simulating-everest>`_.

In order to run your own modifications of the NodeRed flow, best create a
backup of the JSON file located here:

.. code-block:: bash

  ~/checkout/everest-workspace/everest-core/config/nodered/config-sil-flow.json

To do the modifications, best use the NodeRed GUI that you can reach via
browser with URL `http://localhost:1880`.

If you are ready with your changes, download your new JSON file with the help
of the NodeRed GUI:

- Open menu in right upper corner
- Click on `Export`
- Choose the tab `JSON`
- Click `Download` button

The file is now in your local Download folder. To be used as your new flow
process, you will have to move it to the config folder and register it in the
Docker container:

.. code-block:: bash

  mv {Downloads Directory}/config-sil-flow.json {EVerest Workspace Directory}/everest-core/config/nodered/config-sil-flow.json
  cd {EVerest Workspace Directory}/everest-core/build/dist/share/everest/docker
  docker build -t everest-nodered .
  docker stop everest_nodered
  docker run --rm --network host --name everest_nodered --mount type=bind,source=/{EVerest Workspace Directory}/everest-core/config/nodered/config-sil-flow.json,target=/data/flows.json everest-nodered

Your modified flow will now be used for your Simulation GUI at `http://localhost:1880/ui`.

.. hint:: 

  We will add additional documentation here soon to get you an idea about how your own flow can be created and how everything can be wired together. If
  you want to contribute such information, you can of course create a Pull
  Request in the `EVerest` GitHub repository of the EVerest project. (This tutorial can be found in the following directory: `docs/tutorials/run_sil/`)

