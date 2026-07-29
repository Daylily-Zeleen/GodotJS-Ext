#!/usr/bin/env python3
"""
Generate templates.gen.h from TypeScript template files.

Scans GodotJS/weaver-editor/templates/*/*.ts.cs files and generates
a C++ header file with template definitions for the script language.
"""

import os
import sys
import re
from pathlib import Path


def escape_c_string(content: str) -> str:
    """Escape a string for C++ string literal."""
    # Escape backslashes first
    content = content.replace('\\', '\\\\')
    # Escape double quotes
    content = content.replace('"', '\\"')
    # Escape newlines
    content = content.replace('\n', '\\n')
    content = content.replace('\r', '\\r')
    # Escape tabs
    content = content.replace('\t', '\\t')
    return content


def parse_template_file(filepath: Path) -> dict:
    """Parse a template file and extract metadata."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Extract description from first line (if it starts with // meta-description:)
    description = ""
    first_line = content.split('\n')[0].strip()
    if first_line.startswith('// meta-description:'):
        description = first_line[len('// meta-description:'):].strip()

    # Extract inherit from directory name
    inherit = filepath.parent.name

    # Extract name from filename (without extension)
    name = filepath.stem

    # Extract content (everything after the first line)
    content_lines = content.split('\n')
    if len(content_lines) > 1:
        content = '\n'.join(content_lines[1:])
    else:
        content = ""

    return {
        'inherit': inherit,
        'name': name,
        'description': description,
        'content': content
    }


def generate_templates_header(templates_dir: Path, output_file: Path):
    """Generate templates.gen.h from template files."""
    # Find all .ts.cs files
    template_files = list(templates_dir.glob('**/*.ts.cs'))

    if not template_files:
        print(f"Warning: No template files found in {templates_dir}")
        return

    print(f"Found {len(template_files)} template files")

    # Generate the C++ header
    output_lines = [
        "// AUTO-GENERATED",
        "",
        "#include <godot_cpp/templates/local_vector.hpp>",
        "#include <godot_cpp/variant/variant.hpp>",
        "",
        "static godot::LocalVector<godot::Dictionary> &get_script_templates() {",
        "    using namespace godot;",
        "    static LocalVector<Dictionary> templates = [] {",
        "        auto make_template = [](const char *p_inherit, const char *p_name, const char *p_description, const char *p_content,",
        "                                 int p_id = 0, // 随原版使用默认值 0 即可",
        "                                 int origin = 0 // 随原版使用默认值 0 即可(TemplateLocation::TEMPLATE_BUILT_IN)",
        "                                 ) -> Dictionary {",
        "            Dictionary script_tempalte;",
        "            script_tempalte[\"inhert\"] = p_inherit;",
        "            script_tempalte[\"name\"] = p_name;",
        "            script_tempalte[\"description\"] = p_description;",
        "            script_tempalte[\"content\"] = p_content;",
        "            script_tempalte[\"id\"] = p_id;",
        "            script_tempalte[\"origin\"] = origin;",
        "            return script_tempalte;",
        "        };",
        "",
        "        LocalVector<Dictionary> ret = {"
    ]

    # Add each template
    for filepath in sorted(template_files):
        template = parse_template_file(filepath)
        escaped_content = escape_c_string(template['content'])
        output_lines.append(
            f"            make_template(\"{template['inherit']}\", \"{template['name']}\", "
            f"\"{template['description']}\", \"{escaped_content}\"),"
        )

    output_lines.extend([
        "        };",
        "",
        "        return ret;",
        "    }();",
        "",
        "    return templates;",
        "}"
    ])

    # Write to file
    output_content = '\n'.join(output_lines)

    # Check if file exists and has same content
    if output_file.exists():
        with open(output_file, 'r', encoding='utf-8') as f:
            existing_content = f.read()
        if existing_content == output_content:
            print(f"generate {output_file}: no diff")
            return

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(output_content)

    print(f"generating {output_file}")


def main():
    if len(sys.argv) < 2:
        print("Usage: generate_templates_header.py <templates_dir> [output_file]")
        sys.exit(1)

    templates_dir = Path(sys.argv[1])

    if not templates_dir.exists():
        print(f"Error: Directory not found: {templates_dir}")
        sys.exit(1)

    # Default output file
    output_file = templates_dir / "templates.gen.h"

    generate_templates_header(templates_dir, output_file)


if __name__ == "__main__":
    main()
