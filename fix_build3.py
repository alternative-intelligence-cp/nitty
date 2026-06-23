import re
import os
import glob

# Read preprocessed lines
with open("test_ssh_session_preprocessed.npk", "r") as f:
    prep_lines = f.readlines()

# Read new_out.log
with open("new_out.log", "r") as f:
    log_data = f.read()

# Strip ANSI escape sequences
ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
log_data = ansi_escape.sub('', log_data)

# Find all original .npk files
src_files = glob.glob("src/**/*.npk", recursive=True) + glob.glob("tests/**/*.npk", recursive=True)
src_lines = {}
for sf in src_files:
    with open(sf, "r") as f:
        src_lines[sf] = f.readlines()

errors = re.findall(r"error: Line (\d+), Column (\d+): (.*)", log_data)
print(f"Found {len(errors)} errors")
for line_str, col_str, msg in errors:
    line_num = int(line_str)
    if line_num <= len(prep_lines):
        code_line = prep_lines[line_num - 1].strip()
        print(f"Line {line_num}: {msg}\n  Code: {code_line}")
        
        # Try to find this exact line in the source files
        for sf, lines in src_lines.items():
            for i, l in enumerate(lines):
                if l.strip() == code_line:
                    print(f"  -> Found in {sf}:{i+1}")
                    
                    # Fix logic based on error message
                    if "Symbol 'env_get' not found" in msg:
                        src_lines[sf][i] = l.replace("{env_get}", "{get}")
                    elif "Cannot silently unwrap Result" in msg:
                        m = re.search(r"(\w+:\w+\s*=\s*)(.+)", l)
                        if m:
                            src_lines[sf][i] = l[:m.start(2)] + "raw " + m.group(2) + (";\n" if not m.group(2).endswith(";") else "")
                            if src_lines[sf][i][-2:] != ";\n" and l.endswith(";\n"):
                                src_lines[sf][i] = src_lines[sf][i].rstrip() + ";\n"
                        else:
                            # Might be variable = ... instead of Type:variable = ...
                            m2 = re.search(r"(\w+\s*=\s*)(.+)", l)
                            if m2:
                                src_lines[sf][i] = l[:m2.start(2)] + "raw " + m2.group(2) + (";\n" if not m2.group(2).endswith(";") else "")
                                if src_lines[sf][i][-2:] != ";\n" and l.endswith(";\n"):
                                    src_lines[sf][i] = src_lines[sf][i].rstrip() + ";\n"
                    elif "'raw' argument must be Result<T>" in msg:
                        src_lines[sf][i] = l.replace(" raw ", " ")
                        src_lines[sf][i] = src_lines[sf][i].replace("(raw ", "(")
                        src_lines[sf][i] = src_lines[sf][i].replace("=raw ", "=")
                        src_lines[sf][i] = src_lines[sf][i].replace("= raw ", "= ")
                    elif "Cannot call non-function type" in msg:
                        src_lines[sf][i] = l.replace("raw ", "")
                    elif "Cannot initialize variable" in msg and "int32" in msg and "int64" in msg:
                        src_lines[sf][i] = l.replace("int32:", "int64:")

# Write back
for sf, lines in src_lines.items():
    with open(sf, "w") as f:
        f.writelines(lines)

print("Applied fixes.")
