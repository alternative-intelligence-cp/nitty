import re
import os

def expand(path, seen):
    if path in seen:
        return ""
    seen.add(path)
    try:
        with open(path, 'r') as f:
            lines = f.read().splitlines()
    except Exception as e:
        return f"// ERROR: {e}"
    
    out = []
    for line in lines:
        m = re.match(r'^use\s+"([^"]+)"\.\*;', line)
        if m:
            rel = m.group(1)
            # resolve relative to current file
            dir = os.path.dirname(path)
            new_path = os.path.abspath(os.path.join(dir, rel))
            out.append(f"// --- START {new_path} ---")
            out.extend(expand(new_path, seen).splitlines())
            out.append(f"// --- END {new_path} ---")
        elif re.match(r'^use\s+"([^"]+)".*\{', line):
            m2 = re.match(r'^use\s+"([^"]+)".*\{', line)
            rel = m2.group(1)
            dir = os.path.dirname(path)
            new_path = os.path.abspath(os.path.join(dir, rel))
            out.append(f"// --- START {new_path} ---")
            out.extend(expand(new_path, seen).splitlines())
            out.append(f"// --- END {new_path} ---")
        else:
            out.append(line)
    return "\n".join(out)

expanded = expand('/home/randy/Workspace/REPOS/nitty/tests/ssh/test_ssh_session.npk', set())
with open('expanded_session.npk', 'w') as f:
    f.write(expanded)

lines = expanded.splitlines()
if len(lines) >= 603:
    print("LINE 603: " + lines[602])
    print("CONTEXT: ")
    for i in range(598, 608):
        print(f"{i+1}: {lines[i]}")
