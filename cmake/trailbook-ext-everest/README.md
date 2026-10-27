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
source directory (`extension-helpers.cmake`). At the end of each configure run,
the files recorded by the previous run that no generator registered again are
deleted, so a removed interface, type, module or handwritten page does not
linger in the glob toctrees. The API reference generator cleans its own page
directories the same way.

## Dependencies of the generators

The commands that render interface, type, module and API pages depend on
their own inputs only (the definition file, the templates, the script),
not on the outputs of the prepare stage. They write only the files they
register, so they do not need the stem copy to have happened first, and an
edit of a handwritten page does not regenerate the reference.

Each command is wrapped in a custom target because CMake only keeps a
custom command whose output some target in the same directory depends on,
and the module commands are defined below `modules/<Category>/`. The
wrapper targets and the generated files are what the Sphinx stage depends
on.
