# Trailbook Extension for Everest

This CMake package is an extension for the Trailbook CMake package
that provides additional functionality specifically for building
documentation for the Everest project.

The following additional features are provided:

* cmake function: `trailbook_ev_add_module_explanation()`
* cmake function: `trailbook_ev_create_snapshot()`
* cmake function: `trailbook_ev_generate_api_doc()`
* cmake function: `trailbook_ev_generate_rst_from_manifest()`
* cmake function: `trailbook_ev_generate_rst_from_interface()`
* cmake function: `trailbook_ev_generate_rst_from_types()`

Check out the inline documentation of the functions for more details
on how to use them. Each function is defined in its own CMake file
located in this package directory.

## Generated files

Every generator registers the files it writes into the trailbook instance
source directory (`generated-files.cmake`). At the end of each configure run,
the files recorded by the previous run that no generator registered again are
deleted, so a removed interface, type, module or handwritten page does not
linger in the glob toctrees. The API reference generator cleans its own page
directories the same way.
