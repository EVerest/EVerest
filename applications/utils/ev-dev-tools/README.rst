=========================
EVerest development tools
=========================

This project provides ``ev-cli``, the code generator EVerest's build runs to
turn ``types/*.yaml``, ``interfaces/*.yaml`` and module manifests into C++.

Install
-------

::

    python3 -m pip install .

How it is put together
----------------------

::

    CLI  ──▶  loader/validate  ──▶  IR  ──▶  backend  ──▶  files
              schema/           ir/       backends/
                  │
                  ▼
              diagnostics ──▶ types conformance-report

The definitions are read and validated once, turned into an explicit
intermediate representation, and handed to a backend.  The IR describes what a
definition *means* in JSON Schema terms and knows nothing about C++: mapping
``integer`` onto ``int32_t`` is the C++ backend's business, which is what makes
another target -- Rust, OpenAPI, documentation -- an addition rather than a
second parser.

``--backend`` selects the target.  ``cpp`` is the default and generates the
headers everest-core compiles against; ``ir-dump`` writes the IR out as JSON,
which is useful for seeing what the loader made of a definition::

    ev-cli types generate-headers --backend ir-dump -o /tmp/ir

Commands
--------

Every command accepts ``--work-dir``/``-wd``, ``--everest-dir``/``-ed``,
``--schemas-dir``/``-sd``, ``--licenses``/``-lc``, ``--build-dir``/``-bd``,
``--clang-format-file`` and ``--disable-clang-format``.  Commands and actions
have short aliases; ``ev-cli --help`` lists them.

``ev-cli types generate-headers``
    One ``generated/types/<unit>.hpp`` per type file.

``ev-cli interface generate-headers``
    ``Implementation.hpp`` (the implementer's view), ``Interface.hpp`` (the
    caller's view) and ``Types.hpp`` (the type headers it needs) per interface.

``ev-cli module generate-loader <module>``
    The ``ld-ev.hpp``/``ld-ev.cpp`` glue that hooks a module up to the
    framework.  Run by the build; not something to edit.

``ev-cli module create <module>`` / ``ev-cli module update <module>``
    Write and refresh a module's skeleton from its manifest: ``CMakeLists.txt``,
    ``<Module>.hpp``/``.cpp``, one ``<Interface>Impl.hpp``/``.cpp`` per provided
    interface, and a documentation placeholder.  See `Creating a module`_.

``ev-cli types conformance-report``
    Reports where the definitions rely on EVerest's own reading of JSON Schema.
    See `The definition dialect`_.

``ev-cli helpers ...``
    ``generate-uuids``, ``yaml2json``, ``json2yaml``.

``ev-cli {types,interface,module} get-templates``
    Prints the template files a command renders, which the build uses as
    dependencies so that editing a template regenerates its output.

Creating a module
-----------------

Given ``./modules/Example/manifest.yaml``::

    ev-cli module create Example

writes the skeleton beside the manifest.  A subdirectory is created for each
provided interface, named after the implementation id, holding the class that
derives from the generated ``Implementation.hpp``.  What happens next is up to
you: fill in the ``.cpp`` files.

When the manifest or an interface it uses changes::

    ev-cli module update Example

The rules are worth knowing, because they are what protects your work:

* ``.cpp`` files are **never** replaced by ``update``, and neither is the
  documentation placeholder.  Recreating one is a deliberate
  ``module create --force --only <name>``.
* Headers and ``CMakeLists.txt`` are regenerated, but the regions between
  ``ev@<uuid>`` marker comments are carried over from the file being replaced.
  Anything you write inside them survives.
* A file whose markers are malformed is refused rather than regenerated.
* An existing license header is kept, even when the manifest names a different
  license.

Useful options: ``--force`` to overwrite, ``--diff`` (``--dry-run``) to show
what would change without writing, and ``--only`` with a comma separated list
of file names.  ``--only which`` lists the available names::

    ev-cli module create Example --only which
    ev-cli module create Example --only cmakelists,main.cpp --force

The definition dialect
----------------------

EVerest's type definitions are JSON Schema, but they lean on two things no
standard tool does.  Cross-type references are written ``/units#/Power``, whose
resolution -- a search across the ``--everest-dir`` roots, and a fragment
addressing the contents of the ``types:`` key rather than the document -- is
EVerest's own.  And nearly every reference carries sibling keywords such as
``type``, which draft-07 ignores and JSON Schema 2020-12 (and so OpenAPI 3.1)
applies.

``ev-cli`` accepts all of that and reports it::

    ev-cli types conformance-report --verbose

Each entry says what to write instead.  Two categories are worth attention
rather than only counting: a sibling ``type`` that *contradicts* the referenced
type, and ``additionalProperties: false`` beside a reference to an object.
Both are inert today and would change meaning if the definitions were ever read
with OpenAPI semantics.

The loader also accepts the conformant spelling already -- ``#/types/Power``
within a file, ``units.yaml#/types/Power`` across files -- so the definitions
can migrate one file at a time without any consumer noticing.  Nothing
downstream can tell which spelling a file used.

Tests
-----

::

    python3 -m pip install -e '.[test]'
    python3 -m pytest

``tests/test_cli_contract.py`` drives the installed ``ev-cli`` as a black box
and pins what EVerest's CMake and Bazel builds rely on: the shape of
``--version``, the ``get-templates`` output, and the exact set of files each
command produces.  It reads ``EV_CLI`` to choose which build to test, so it can
be pointed at another one for comparison.

``tests/test_conformance.py`` asserts the diagnostic counts over the real
definitions.  They are a ratchet: lowering them is progress, raising them means
the legacy dialect was reintroduced.

``tests/test_cpp_backend.py`` compares generated output against the golden files
in ``tests/fixtures/golden``.  Set ``EV_CLI_UPDATE_GOLDEN=1`` to rewrite them
after an intentional change -- and then read the diff, because it is the
generated API that every module compiles against.
