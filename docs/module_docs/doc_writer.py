#!/usr/bin/env python

import json, os
from jinja2 import BaseLoader, Environment

# path definition
out_dir = "docs/generated/"
manifests_dir = "docs/dumped_manifests/"
templates_dir = "./docs/module_docs/templates/"
if not os.path.exists(out_dir):
    os.makedirs(out_dir)
if not os.path.exists(out_dir + "modules/"):
    os.mkdir(out_dir + "modules/")

# create module list
module_list = list()
file_list = os.listdir(manifests_dir)
for entry in file_list:
    if entry.endswith(".json") and entry != "config.json":
        module_list.append(entry.split(".")[0])
module_list = sorted(module_list)

# doc all modules
f = open(templates_dir + "module.rst.jinja")
template_str = f.read()
env = Environment(loader=BaseLoader())
template = env.from_string(template_str)

for module in module_list:
    print("Doc module " + module)
    f = open(manifests_dir + module + ".json")
    manifest_str = f.read()
    manifest = json.loads(manifest_str)
    output = template.render(name=module, manifest=manifest)
    f = open(out_dir + "modules/" + module + ".rst", "w")
    f.write(output)
    f.close()

f = open(templates_dir + "everest_modules.rst.jinja")
template_str = f.read()
env = Environment(loader=BaseLoader())
template = env.from_string(template_str)
output = template.render(module_list=module_list)
f = open(out_dir + "everest_modules.rst", "w")
f.write(output)
f.close()
