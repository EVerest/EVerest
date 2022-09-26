#!/usr/bin/env python

import argparse, os
import jinja2, json

# Jinja filters
def rst_indent(input):
    lines = input.splitlines()
    lines = [f"| { line }\r\n" for line in lines]
    return "".join(lines)
def make_rst_ref(input):
    output = input.replace("/", "")
    output = output.replace("#", "-")
    return output

def process_index_file(
    template:jinja2.Template,
    directory:str,
    entry_ref_prefix:str,
    refname:str,
    headline:str,
    out_path:str,
    templates_dir:str
):
    dir_list = os.listdir(directory)
    file_list = []
    for entry in dir_list:
        if entry.endswith(".json") and entry != "config.json":
            name = entry.split(".")[0]
            file_list.append(f"{ entry_ref_prefix }/{ name }")
    output = template.render(
        file_list=file_list, 
        refname=refname,
        headline=headline,
        templates_path=templates_dir
    )
    with open(out_path, "w") as f:
        f.write(output)
        f.close()

def process_directory(
    template:jinja2.Template, 
    directory:str, 
    out_dir:str,
    templates_dir:str,
    handwritten_modules:list
):
    if handwritten_modules is None:
        handwritten_modules = []
    
    file_list = os.listdir(directory)
    for entry in file_list:
        if entry.endswith(".json") and entry != "config.json":
            name = entry.split(".")[0]
            include_handwritten = False
            if name in handwritten_modules:
                include_handwritten = True
            print(f"Process { directory }/{ entry }")
            with open(f"{ directory }/{ entry }") as f:
                data = json.load(f)
                output = template.render(
                    name=name,
                    data=data,
                    include_handwritten=include_handwritten,
                    templates_path=templates_dir
                )
            with open(f"{ out_dir }/{ name }.rst", "w") as f:
                f.write(output)
                f.close()

def main():    
    parser = argparse.ArgumentParser(description='Process JSON files and jinja2 templates to generate documentation.')
    parser.add_argument(
        '--out-directory',
        '-o',
        type=os.path.abspath,
        dest='out_dir',
        action='store',
        default="generated/",
        help="Output directory for generated files"
    )
    parser.add_argument(
        '--json-directory',
        '-j',
        type=os.path.abspath,
        dest='json_dir',
        action='store',
        required=True,
        help="Directory containing JSON files"
    )
    parser.add_argument(
        '--template-directory',
        '-t',
        type=os.path.abspath,
        dest='templates_dir',
        action='store',
        required=True,
        help="Directory containing jinja2 templates"
    )
    parser.add_argument(
        '--handwritten-modules-docs',
        '-m',
        type=os.path.abspath,
        dest='handwritten_dir',
        action='store',
        default='',
        help="Directory containing handwritten documentation for modules"
    )
    
    args = parser.parse_args()

    if not os.path.exists(args.json_dir):
        raise  FileNotFoundError(f"Missing json path: { args.json_dir } doesn't exist")
    if not os.path.exists(args.templates_dir):
        raise FileNotFoundError(f"Missing templates path: { args.template_dir } doesn't exist")

    if not os.path.exists(args.out_dir):
        os.makedirs(args.out_dir)
    if not os.path.exists(f"{ args.out_dir }/types/"):
        os.mkdir(f"{ args.out_dir }/types/")
    if not os.path.exists(f"{ args.out_dir }/interfaces/"):
        os.mkdir(f"{ args.out_dir }/interfaces/")
    if not os.path.exists(f"{ args.out_dir }/modules/"):
        os.mkdir(f"{ args.out_dir }/modules/")
    
    file_list = []
    if args.handwritten_dir != '':
        args.handwritten_dir = os.path.abspath(args.handwritten_dir)
        if not os.path.exists(args.handwritten_dir):
            raise FileNotFoundError(f"No valid handwritten modules path: { args.handwritten_dir } doesn't exist")
        file_list = os.listdir(args.handwritten_dir)
    
    handwritten_modules = [file.split(".")[0] for file in file_list if file.endswith(".rst")]
    
    loader = jinja2.FileSystemLoader(args.templates_dir)
    env = jinja2.Environment(
        loader=loader, 
        trim_blocks=True, 
        lstrip_blocks=True
    )
    env.filters['rst_indent'] = rst_indent
    env.filters['make_rst_ref'] = make_rst_ref
    
    interface_template = env.get_template("interface.rst.jinja")
    process_directory(
        interface_template,
        f"{ args.json_dir }/interfaces",
        f"{ args.out_dir }/interfaces",
        args.templates_dir,
        handwritten_modules
    )
    types_template = env.get_template("types.rst.jinja")
    process_directory(
        types_template,
        f"{ args.json_dir }/types",
        f"{ args.out_dir }/types",
        args.templates_dir,
        handwritten_modules
    )

    link_to_handwritten = os.listdir
    module_template = env.get_template("module.rst.jinja")
    process_directory(
        module_template,
        f"{ args.json_dir }/manifests",
        f"{ args.out_dir }/modules",
        args.templates_dir,
        handwritten_modules
    )

    index_template = env.get_template("file_list_ref.rst.jinja")
    process_index_file(
        index_template,
        f"{ args.json_dir }/interfaces",
        "interfaces",
        "everest_interfaces",
        "EVerest Interfaces",
        f"{ args.out_dir }/everest_interfaces.rst",
        args.templates_dir

    )
    process_index_file(
        index_template,
        f"{ args.json_dir }/types",
        "types",
        "everest_types",
        "EVerest Types",
        f"{ args.out_dir }/everest_types.rst",
        args.templates_dir
    )
    process_index_file(
        index_template,
        f"{ args.json_dir }/manifests",
        "modules",
        "everest_modules",
        "EVerest Modules",
        f"{ args.out_dir }/everest_modules.rst",
        args.templates_dir
    )

if __name__ == "__main__":
    main()
