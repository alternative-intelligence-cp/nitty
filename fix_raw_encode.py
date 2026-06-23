import os

for root, _, files in os.walk('/home/randy/Workspace/REPOS/nitty'):
    for file in files:
        if file.endswith('.npk'):
            path = os.path.join(root, file)
            with open(path, 'r') as f:
                content = f.read()
            if 'SSH_encode_uint32' in content:
                # Replace assignment without raw
                new_content = content.replace('= SSH_encode_uint32', '= raw SSH_encode_uint32')
                new_content = new_content.replace('= nitpick_SSH_encode_uint32', '= raw SSH_encode_uint32')
                if new_content != content:
                    with open(path, 'w') as f:
                        f.write(new_content)
                    print(f"Fixed {path}")
