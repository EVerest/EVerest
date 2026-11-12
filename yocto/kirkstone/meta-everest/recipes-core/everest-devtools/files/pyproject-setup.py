# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Generic setup.py for setuptools < 61, which cannot read PEP 621 metadata out
# of the [project] table of pyproject.toml. Instead of mirroring pyproject.toml
# by hand, read it at build time and translate the subset of [project] and
# [tool.setuptools] that we need into setup() keyword arguments. Anything this
# shim does not understand is a hard error, so upstream changes cannot silently
# produce an incomplete wheel.

import os
from glob import glob

try:
    import tomllib
except ImportError:  # Python < 3.11
    import tomli as tomllib

from setuptools import find_packages, setup

HERE = os.path.dirname(os.path.abspath(__file__))

with open(os.path.join(HERE, 'pyproject.toml'), 'rb') as f:
    pyproject = tomllib.load(f)

project = dict(pyproject['project'])
tool = dict(pyproject.get('tool', {}).get('setuptools', {}))
kwargs = {}


def take(table, key, default=None):
    return table.pop(key, default)


# --- [tool.setuptools] -------------------------------------------------------

find = take(tool, 'packages', {}).get('find', {})
where = find.get('where', ['.'])
if len(where) != 1:
    raise RuntimeError('pyproject-setup.py: only one packages.find.where entry is supported')
where = where[0]
kwargs['packages'] = find_packages(
    where=where,
    include=find.get('include', ['*']),
    exclude=find.get('exclude', ()),
)
if where != '.':
    kwargs['package_dir'] = {'': where}
SRC = os.path.join(HERE, where)


def expand_package_data(package, patterns):
    """setuptools only expands '**' in package_data from 62.3.0 on."""
    base = os.path.join(SRC, package.replace('.', os.sep))
    return sorted({
        os.path.relpath(path, base)
        for pattern in patterns
        for path in glob(os.path.join(base, pattern), recursive=True)
        if os.path.isfile(path)
    })


package_data = take(tool, 'package-data', {})
if package_data:
    kwargs['package_data'] = {
        pkg: expand_package_data(pkg, patterns) for pkg, patterns in package_data.items()
    }

dynamic_cfg = take(tool, 'dynamic', {})

if tool:
    raise RuntimeError('pyproject-setup.py: unsupported [tool.setuptools] keys: %s' % sorted(tool))

# --- [project] ---------------------------------------------------------------


def attr_value(spec):
    """Resolve 'pkg.module.__version__' by scanning the source, without importing it."""
    module, _, name = spec.rpartition('.')
    path = os.path.join(SRC, *module.split('.'))
    path = os.path.join(path, '__init__.py') if os.path.isdir(path) else path + '.py'
    with open(path) as f:
        for line in f:
            if line.split('=', 1)[0].strip() == name:
                return line.split('=', 1)[1].strip().strip('\'"')
    raise RuntimeError('pyproject-setup.py: could not resolve attr %s' % spec)


for field in take(project, 'dynamic', []):
    cfg = dynamic_cfg.get(field)
    if field == 'version' and cfg and 'attr' in cfg:
        kwargs['version'] = attr_value(cfg['attr'])
    else:
        raise RuntimeError('pyproject-setup.py: unsupported dynamic field %s' % field)

kwargs['name'] = take(project, 'name')
for key in ('version', 'description'):
    if key in project:
        kwargs[key] = take(project, key)

readme = take(project, 'readme')
if readme is not None:
    if isinstance(readme, str):
        readme = {'file': readme}
    if 'file' in readme:
        with open(os.path.join(HERE, readme['file'])) as f:
            kwargs['long_description'] = f.read()
    else:
        kwargs['long_description'] = readme['text']
    if 'content-type' in readme:
        kwargs['long_description_content_type'] = readme['content-type']

if 'requires-python' in project:
    kwargs['python_requires'] = take(project, 'requires-python')
kwargs['install_requires'] = take(project, 'dependencies', [])
kwargs['extras_require'] = take(project, 'optional-dependencies', {})
kwargs['classifiers'] = take(project, 'classifiers', [])
kwargs['keywords'] = take(project, 'keywords', [])

license_ = take(project, 'license')
if isinstance(license_, dict):
    kwargs['license'] = license_.get('text')
elif license_:
    kwargs['license'] = license_


def people(key, name_kw, email_kw):
    entries = take(project, key, [])
    names = [p['name'] for p in entries if 'name' in p]
    emails = [p['email'] for p in entries if 'email' in p]
    if names:
        kwargs[name_kw] = ', '.join(names)
    if emails:
        kwargs[email_kw] = ', '.join(emails)


people('authors', 'author', 'author_email')
people('maintainers', 'maintainer', 'maintainer_email')

urls = take(project, 'urls', {})
if urls:
    kwargs['project_urls'] = urls
    kwargs['url'] = urls.get('Homepage', next(iter(urls.values())))

entry_points = {}
for table, group in (('scripts', 'console_scripts'), ('gui-scripts', 'gui_scripts')):
    scripts = take(project, table, {})
    if scripts:
        entry_points[group] = ['%s = %s' % item for item in scripts.items()]
for group, eps in take(project, 'entry-points', {}).items():
    entry_points[group] = ['%s = %s' % item for item in eps.items()]
if entry_points:
    kwargs['entry_points'] = entry_points

if project:
    raise RuntimeError('pyproject-setup.py: unsupported [project] keys: %s' % sorted(project))

setup(**kwargs)
