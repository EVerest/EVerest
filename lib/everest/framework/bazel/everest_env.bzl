load("@rules_python//python:defs.bzl", "PyInfo")
load("//third-party/bazel/toolchains:defs.bzl", "CROSS_PYTHON_INCOMPATIBLE")

def _everest_env(ctx):
    """Everest Root rule

    Rule creates a everest root from provided modules and config file.
    """

    # Validate the input - we make sure that the set of provided modules matches
    # the set of modules declared in config.yaml
    validation_output = ctx.actions.declare_file(ctx.attr.name + ".validation")
    ctx.actions.run(
        inputs = ctx.attr.config_file[DefaultInfo].files,
        outputs = [validation_output],
        executable = ctx.executable._validation_tool,
        arguments = [
                        "--output",
                        validation_output.path,
                        "--config",
                        ctx.attr.config_file[DefaultInfo].files.to_list()[0].path,
                        "--",
                    ] +
                    [mod.label.name for mod in ctx.attr.modules],
    )

    symlinks = {}
    files = []
    py_toolchain = ctx.toolchains["@bazel_tools//tools/python:toolchain_type"]
    py_interpreter = py_toolchain.py3_runtime.interpreter.dirname.removeprefix("external/")
    py_imports = []
    py_transitive_sources = []

    # Python modules get a special handling.
    for mod in ctx.attr.modules + ctx.attr.test_modules:
        if PyInfo in mod:
            py_imports.extend(mod[PyInfo].imports.to_list())
            py_transitive_sources.extend(mod[PyInfo].transitive_sources.to_list())

    for mod in ctx.attr.modules + ctx.attr.test_modules:
        # Find the manifest in the data_runfiles and use its path as prefix.
        manifest = [
            file
            for file in mod[DefaultInfo].data_runfiles.files.to_list()
            if file.basename in ["manifest.yaml", "manifest.yml"]
        ][0]
        prefix = manifest.dirname

        symlinks.update(
            {
                "libexec/everest/modules/{0}{1}".format(
                    mod.label.name,
                    file.path.removeprefix(prefix),
                ): file
                for file in mod[DefaultInfo].data_runfiles.files.to_list()
                if file.path.startswith(prefix)
            },
        )
        [
            files.append(file)
            for file in mod[DefaultInfo].default_runfiles.files.to_list()
            if not file.path.startswith(prefix)
        ]

    config_file = ctx.attr.config_file[DefaultInfo].files.to_list()[0]
    config_path = "etc/everest/{0}".format(config_file.basename)
    symlinks.update({"bin/manager_impl": ctx.attr.manager[DefaultInfo].files.to_list()[0]})
    symlinks.update(
        {
            config_path: config_file,
        },
    )
    symlinks.update(
        {
            "etc/everest/default_logging.cfg": ctx.attr.default_logging_file[DefaultInfo].files.to_list()[0],
        },
    )

    # EVerest expects that there is a `share/everest/www` directory but does
    # not care about the content... We just symlink the config.yaml into it.
    symlinks.update(
        {
            "share/everest/www/config.yaml": ctx.attr.config_file[DefaultInfo].files.to_list()[0],
        },
    )
    symlinks.update(
        {
            "share/everest/schemas/{0}".format(file.basename): file
            for file in ctx.attr.schemas[DefaultInfo].files.to_list()
        },
    )
    symlinks.update(
        {
            "share/everest/interfaces/{0}".format(file.basename): file
            for interfaces in ctx.attr.interfaces
            for file in interfaces[DefaultInfo].files.to_list()
        },
    )
    symlinks.update(
        {
            "share/everest/types/{0}".format(file.basename): file
            for types in ctx.attr.types
            for file in types[DefaultInfo].files.to_list()
        },
    )
    symlinks.update(
        {
            "share/everest/errors/{0}".format(file.basename): file
            for errors in ctx.attr.errors
            for file in errors[DefaultInfo].files.to_list()
        },
    )

    # For the executable we need to export the python specific variables by
    # hand.
    script = ctx.actions.declare_file("manager_wrapper.{}".format(ctx.label.name))
    script_content = """\
#!/bin/sh
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
export PATH="$SCRIPT_DIR/{py_interpreter}:$PATH"
{pythonpath_lines}
""".format(
        py_interpreter = py_interpreter.removeprefix("external/"),
        pythonpath_lines = "\n".join([
            'export PYTHONPATH="$SCRIPT_DIR/{0}:$PYTHONPATH"'.format(imp)
            for imp in py_imports
        ]),
    )

    if ctx.attr._is_test:
        script_content += """
exec bin/manager_impl --prefix . --config {0} --check
        """.format(config_path)
    else:
        script_content += """
exec bin/manager_impl "$@"
""".format(config_path)
    ctx.actions.write(script, script_content, is_executable = True)
    symlinks.update({"bin/manager": script})

    runfiles = ctx.runfiles(
        symlinks = symlinks,
        files = files + [script],
    )

    return [
        DefaultInfo(
            executable = script,
            runfiles = runfiles,
        ),
        OutputGroupInfo(_validation = depset([validation_output])),
        PyInfo(
            imports = depset(py_imports),
            transitive_sources = depset(py_transitive_sources),
        ),
    ]

ATTRS = {
    "config_file": attr.label(
        doc = """
The EVerest configuration file. It will be linked to
`/etc/everest/<basename>`""",
        allow_single_file = True,
    ),
    "manager": attr.label(
        doc = "The EVerest manager.",
        default = Label("//lib/everest/framework:manager"),
        allow_single_file = True,
        executable = True,
        cfg = "target",
    ),
    "schemas": attr.label(
        doc = "The target with the EVerest schemas.",
        default = Label("//lib/everest/framework/schemas"),
    ),
    "interfaces": attr.label_list(
        doc = "A list of targets with EVerest interfaces.",
        default = [
            Label("//lib/everest/framework/everestrs/tests/interfaces"),
        ],
    ),
    "types": attr.label_list(
        doc = "A list of targets with EVerest types.",
        default = [
            Label("//lib/everest/framework/everestrs/tests/types"),
        ],
    ),
    "errors": attr.label_list(
        doc = "A list of targets with EVerest errors.",
        default = [
            Label("//lib/everest/framework/everestrs/tests/errors"),
        ],
    ),
    "default_logging_file": attr.label(
        doc = "The target with the EVerest logging.ini file.",
        default = Label("//lib/everest/framework/everestrs/tests:logging.ini"),
        allow_single_file = True,
    ),
    "modules": attr.label_list(
        doc = """
The list of targets with the EVerest modules under test.

The rule validates that the set of provided modules matches the set of modules
defined in the given `config_file`.""",
        allow_files = False,
    ),
    "test_modules": attr.label_list(
        doc = """
The list of targets with EVerest modules which are only enabled by the
`everest.testing` framework.

The rule will not enforce that these modules are defined in the given
`config_file`.
        """,
        allow_files = False,
    ),
    "_validation_tool": attr.label(
        default = Label("//lib/everest/framework/bazel/validate"),
        executable = True,
        cfg = "exec",
    ),
    "_is_test": attr.bool(
        default = False,
        doc = "Indicates if target is test target to validate config",
    ),
}

everest_impl_env = rule(
    implementation = _everest_env,
    attrs = ATTRS,
    executable = True,
    toolchains = ["@bazel_tools//tools/python:toolchain_type"],
)

_everest_impl_test = rule(
    implementation = _everest_env,
    attrs = dict(ATTRS, _is_test = attr.bool(default = True)),
    toolchains = ["@bazel_tools//tools/python:toolchain_type"],
    doc = """
Creates an EVerest Test.

Example:

Suppose you have the EVerest modules `ModuleFoo` and `ModuleBar` and the
EVerest config `my_config.yaml` which uses both modules. The test will launch
the modules and return when the manager process returns.

Then you can create an environment by writing:

```
everest_test(
    name = "my_everest_env",
    modules = [":ModuleFoo", ":ModuleBar"],
    config_file = ":my_config.yaml",
    test_script=":my_test_script",
)

```

You can run it with `bazel test`.
    """,
    test = True,
)

def everest_test(name, target_compatible_with = [], **kwargs):
    """Wrapper around the everest_test rule that marks the target as
    incompatible with cross-compilation platforms (no Python toolchain)."""
    _everest_impl_test(
        name = name,
        target_compatible_with = CROSS_PYTHON_INCOMPATIBLE + target_compatible_with,
        **kwargs
    )

def everest_env(name, target_compatible_with = [], **kwargs):
    """
    Creates an EVerest environment.

    Example:

    Suppose you have the EVerest modules `ModuleFoo` and `ModuleBar` and the
    EVerest config `my_config.yaml` which uses both modules.

    Then you can create an environment by writing:

    ```
    everest_env(
        name = "my_everest_env",
        modules = [":ModuleFoo", ":ModuleBar"],
        config_file = ":my_config.yaml",
    )

    ```

    You can either run this target with `bazel run` or pass it for example to a (py)
    test which will run your tests against the environment.
    """
    everest_impl_env(
        name = name,
        target_compatible_with = CROSS_PYTHON_INCOMPATIBLE + target_compatible_with,
        **kwargs
    )
    everest_test(name = name + "__manager_test", tags = ["exclusive"], target_compatible_with = target_compatible_with, **kwargs)
