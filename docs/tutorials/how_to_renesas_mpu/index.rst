.. _how_to_renesas_mpu:

##################################
How to for Renesas MPU (RZ/G2L family)
##################################

To get more information on EVerest and Renesas hardware, see here:
https://www.renesas.com/en/products/microcontrollers-microprocessors/rz-mpus/rz-partner-solutions/pionix-basecamp

Here is how to set it up and run an EVerest simulation:

#. Clone the rz-community-bsp repo: https://github.com/renesas-rz/rz-community-bsp.git

#. Apply the following patch, which will add necessary changes to build everest-core:
  `Patch file <./0001-Signed-off-by-sachin.dominic.zn-renesas.com.patch>`_

#. Start kas-container menu to configure the environment:
  #. Run ./kas-container menu
  #. Select device as RZ/G2L.
  #. Save & Exit

#. Start kas-shell with ./kas-container shell:
  #. Run *sudo apt-get update*
  #. Run *sudo apt-get install -y python3.11 python3.11-dev*

#. Edit the file */work/poky/meta/recipes-devtools/elfutils/elfutils_0.186.bb**
  to add the following line:
  *CFLAGS:append = " -Wno-error=deprecated-declarations"*

#. Finally, start the build: *bitbake renesas-image-minimal*
  Once the build is complete, exit the shell.

#. Flash the hardware with the information in the RZ/G2L startup guide, which
  can be found here:
  `https://www.renesas.com/us/en/document/qsg/rzg2l-evaluation-board-kit-quick-start-guide`_

  The generated image/binaries will be present in the directory:
  *built/tmp/deploy/images/smarc-rzg2l*

#. Start EVerest with the following command:

  .. code-block:: bash

    /usr/bin/manager --conf /etc/everest/config-sil.yaml

If everything has been set up correctly, you will be able to run simulation
steps with EVerest now. Have a look at the
:ref:`Quick Start Guide <quickstartguide_main>` to get more information on
this.

From there on, you might want to take it to the next level by implementing
your own customized EVerest modules. Have a look at the dedicated
:ref:`EVerest module concept documentation <moduleconcept_main>` to get an
idea of doing that.
