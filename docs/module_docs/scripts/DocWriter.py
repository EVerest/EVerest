import os
import json
from RstCreator import RstCreator

class DocWriter:
    
    def __init__(self):
        self.out_dir = "../_build/"
        self.raw_json_dir = "../raw_json/"
        self.rst = RstCreator()
        if not os.path.exists(self.out_dir):
            os.mkdir(self.out_dir)

    def create_module_list(self) -> list:
        module_list = list()

        file_list = os.listdir(self.raw_json_dir)
        for entry in file_list:
            if entry.endswith(".json") and entry != "config.json":
                module_list.append(entry.split(".")[0])

        return module_list

    def doc_all_modules(self):
        module_list = self.create_module_list()
        for module in module_list:
            print("Doc the module " + module)
            moduleDocer = self.DocSingleModule(module, self.raw_json_dir, self.out_dir, self.rst)
            moduleDocer.doc_module()

    class DocSingleModule:

        def __init__ (self, module_name: str, raw_json_dir, out_dir, rst):
            self.module_name = module_name
            self.strings = {
                "unit_introduction": "This module provides the following units:\n\n",
                "unit_vars_subsubtitle": "Provides the following vars:",
                "unit_cmds_subsubtitle": "Provides the following cmds:",
                "unit_config_subsubtitle": "Uses the following config:",
                "req_introduction": "This module has the following requirements:\n\n",
                "meta_introduction": "Metadata for this module:\n\n"
            }
            self.raw_json_dir = raw_json_dir
            self.out_dir = out_dir
            self.rst = rst
            # import json file
            json_file = open(self.raw_json_dir + self.module_name + ".json", "r")
            json_string = json_file.read()
            json_file.close()
            self.json_data = json.loads(json_string)

        def doc_module(self):
            file_name = self.out_dir + self.module_name + ".rst"
            if os.path.exists(file_name):
                os.remove(file_name)
            self.file = open(file_name, "x")
            self.doc_begin()
            self.doc_metadata()
            self.doc_requirements()
            self.doc_all_units()
            self.file.close()

        def doc_begin(self):
            self.file.write(self.rst.create_ref_anchor("everest_module_" + self.module_name))
            self.file.write(self.rst.create_title(self.module_name))

        def doc_all_units(self):
            self.file.write(self.strings["unit_introduction"])
            list_of_units = self.json_data["provides"].items()
            for unit in list_of_units:
                self.doc_unit(unit)
        
        def doc_unit(self, unit):
            name = unit[0]
            unit_data = unit[1]

            unit_doc = ""
            unit_doc += self.rst.create_subtitle(name)
            decription = unit_data["description"]
            if decription != "":
                unit_doc += decription + "\n\n"
            # vars
            list_of_vars = unit_data["vars"].items()
            if len(list_of_vars) > 0:
                unit_doc += self.rst.create_subsubtitle(self.strings["unit_vars_subsubtitle"])
                for var in list_of_vars:
                    header = self.rst.write_bold(var[0] + ":") + " " + self.rst.write_italic(var[1]["type"]) + " " + var[1]["description"]
                    unit_doc += self.rst.create_toggle_header(header, 1)
                    unit_doc += self.create_list_of_items(var[1].items(), 3) + "\n"
            # cmds
            list_of_cmds = unit_data["cmds"].items()
            if len(list_of_cmds) > 0:
                unit_doc += self.rst.create_subsubtitle(self.strings["unit_cmds_subsubtitle"])
                for cmd in list_of_cmds:
                    unit_doc += self.rst.write_bold(cmd[0]) + " " + cmd[1]["description"] + "\n"
                    # args
                    list_of_args = cmd[1]["arguments"].items()
                    if len(list_of_args) > 0:
                        unit_doc += " | " + self.rst.write_bold("arguments:") + " \n\n"
                        for arg in list_of_args:
                            header = self.rst.write_bold(arg[0] + ":") + " " + self.rst.write_italic(json.dumps(arg[1]["type"])) + " " + arg[1]["description"]
                            unit_doc += self.rst.create_toggle_header(header, 2)
                            unit_doc += self.create_list_of_items(arg[1].items(), 4) + "\n"
                    # result
                    header = self.rst.write_bold("result:") + " " + self.rst.write_italic(json.dumps(cmd[1]["result"]["type"])) + cmd[1]["result"]["description"]
                    unit_doc += self.rst.create_toggle_header(header, 1)
                    unit_doc += self.create_list_of_items(cmd[1]["result"].items(), 3) + "\n"

            # config
            list_of_configs = unit_data["config"].items()
            if len(list_of_configs) > 0:
                unit_doc += self.rst.create_subsubtitle(self.strings["unit_config_subsubtitle"])
                for config in list_of_configs:
                    header = self.rst.write_bold(config[0] + ":") + " " + self.rst.write_italic(config[1]["type"]) + " " + config[1]["description"]
                    unit_doc += self.rst.create_toggle_header(header, 1)
                    unit_doc += self.create_list_of_items(config[1].items(), 3) + "\n"

            self.file.write(unit_doc)

        def doc_requirements(self):
            req_doc = ""
            list_of_reqs = self.json_data["requires"].items()
            if len(list_of_reqs) > 0:
                req_doc += self.strings["req_introduction"]
                for req in list_of_reqs:
                    header = self.rst.write_bold(req[0] + ":") + " " + self.rst.write_italic(req[1]["class"])
                    req_doc += self.rst.create_toggle_header(header, 1)
                    req_doc += self.create_list_of_items(req[1].items(), 3) + "\n"

            self.file.write(req_doc)

        def doc_metadata(self):
            meta_doc = ""
            list_of_metas = self.json_data["metadata"].items()
            if len(list_of_metas) > 0:
                meta_doc += self.rst.create_toggle_header(self.strings["meta_introduction"], 0)
                meta_doc += self.create_list_of_items(list_of_metas, 2) + "\n"

            self.file.write(meta_doc)

        def create_list_of_items(self, items, spaces) -> str:
            result = ""
            for item in items:
                if item[0] != "description" and item[0] != "type" and item[0] != "class":
                    result += " " * spaces + "| " + item[0] + ": " + json.dumps(item[1]) + "\n"
            return result.translate(str.maketrans({'_': r'\_'}))

if __name__ == "__main__":
    writer = DocWriter()
    writer.doc_all_modules()
