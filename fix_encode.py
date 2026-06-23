import os

for root, _, files in os.walk('/home/randy/Workspace/REPOS/nitty'):
    for file in files:
        if file.endswith('.npk'):
            path = os.path.join(root, file)
            with open(path, 'r') as f:
                content = f.read()
            if 'ssh_encode_uint32' in content:
                new_content = content.replace('ssh_encode_uint32', 'SSH_encode_uint32')
                with open(path, 'w') as f:
                    f.write(new_content)
                print(f"Fixed {path}")
