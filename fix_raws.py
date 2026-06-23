import os

def fix_file(path):
    with open(path, 'r') as f:
        content = f.read()

    new_content = content.replace('raw SSH_recv_packet', 'SSH_recv_packet')
    new_content = new_content.replace('raw SSH_channel_last_id', 'SSH_channel_last_id')

    if new_content != content:
        with open(path, 'w') as f:
            f.write(new_content)
        print(f"Fixed {path}")

for root, _, files in os.walk('/home/randy/Workspace/REPOS/nitty/src'):
    for file in files:
        if file.endswith('.npk'):
            fix_file(os.path.join(root, file))
