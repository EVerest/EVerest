import argparse
from typing import List, NamedTuple
import jinja2
from pathlib import Path

def process_redirect(
    template: jinja2.Template,
    release_name: str,
    out_path: Path
):
    output = template.render(
        latest_release=release_name
    )
    out_path.write_text(output)


def process_versions_index(
    template: jinja2.Template,
    version_list: List[str],
    out_path: Path
):
    output = template.render(
        version_list=version_list
    )
    out_path.write_text(output)

def main():
    parser = argparse.ArgumentParser(description='Process versions_index.html.jinja and place redirect.html in the output directory')
    parser.add_argument(
        '--template-directory',
        '-t',
        type=Path,
        dest='template_dir',
        action='store',
        required=True,
        help="Template directory"
    )
    parser.add_argument(
        '--html-root-directory',
        '-r',
        type=Path,
        dest='html_root_dir',
        action='store',
        required=True,
        help="HTML root directory"
    )
    parser.add_argument(
        '--release-name',
        '-n',
        type=str,
        dest='release_name',
        action='store',
        default=None,
        help="Name of the release"
    )
    args = parser.parse_args()

    if not args.html_root_dir.is_dir():
        raise FileNotFoundError(f"HTML root path: '{ args.html_root_dir }' doesn't exist")

    version_list: List[str] = []
    version_list.extend(
        item.name for item in args.html_root_dir.iterdir() if item.is_dir()
    )
    if not args.template_dir.is_dir():
        raise FileNotFoundError(f"Template path: '{ args.template_dir }' doesn't exist")

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(args.template_dir),
        trim_blocks=True,
        lstrip_blocks=True
    )
    process_versions_index(
        template=env.get_template("versions_index.html.jinja"),
        version_list=version_list,
        out_path=args.html_root_dir / "versions_index.html"
    )

    if args.release_name is not None:
        process_redirect(
            template=env.get_template("redirect.html.jinja"),
            release_name=args.release_name,
            out_path=args.html_root_dir / "index.html"
        )

if __name__ == "__main__":
    main()
