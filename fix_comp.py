import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

# 1. Fix ssh_session.npk
replace_in_file("src/ssh/ssh_session.npk", "ssh_compression_get", "SSH_compression_get")
replace_in_file("src/ssh/ssh_session.npk", "ssh_compression_set", "SSH_compression_set")

# 2. Add compression_get to nitpick_ssh.npk's pub Type:SSH
npk_path = "/home/randy/Workspace/REPOS/nitpick-packages/packages/nitpick-ssh/src/nitpick_ssh.npk"
with open(npk_path, 'r') as file:
    lines = file.readlines()

for i, line in enumerate(lines):
    if "func:compression_set = NIL(int64:session, string:algo)" in line:
        # Insert after this line
        lines.insert(i + 1, "    func:compression_get = string(int64:session) { pass raw ssh_compression_get(session); };\n")
        break

with open(npk_path, 'w') as file:
    file.writelines(lines)

