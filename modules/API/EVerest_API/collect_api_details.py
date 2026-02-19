# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import yaml
import pathlib
from collections import Counter
from typing import Any


def collect_api_details(root_dir: str | pathlib.Path, output_file: str):
    root = pathlib.Path(root_dir)
    api_details: dict[str, Any] = {}

    manifest_paths = sorted(
        root.glob("*_API/manifest.yaml"), 
        key=lambda p: p.parent.name
    )

    ALLOWED_KEYS = {"interface", "min_connections", "max_connections"}

    for manifest_path in manifest_paths:
        folder_name = manifest_path.parent.name
        if folder_name == "EVerest_API":
            continue
        
        with manifest_path.open("r", encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}

        folder_entry = {}

        for section in ("provides", "requires"):
            if section in data and isinstance(data[section], dict):
                formatted_section = {}
                counts = Counter()

                for _, details in data[section].items():
                    # Skip items that don't have an 'interface' key
                    interface_name = details.get("interface")
                    if not interface_name:
                        continue
                    
                    counts[interface_name] += 1
                    new_key = f"{interface_name}_{counts[interface_name]:02d}"
                    
                    # 3. extract the keys of interest
                    filtered_details = {
                        k: v for k, v in details.items() 
                        if k in ALLOWED_KEYS
                    }
                    
                    formatted_section[new_key] = filtered_details

                if formatted_section:
                    folder_entry[section] = formatted_section

        if folder_entry:
            api_details[folder_name] = folder_entry

    # 4. Write to file
    with open(output_file, "w", encoding="utf-8") as out:
        yaml.dump(api_details, out, sort_keys=False, default_flow_style=False)

if __name__ == "__main__":
    # Usage
    collect_api_details(root_dir="..", output_file="apis.yaml")
