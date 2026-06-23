import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

replace_in_file("tests/ssh/test_ssh_forward.npk", 'use "../../src/ssh/ssh_forward.npk".*;', 'use "../../src/ssh/ssh_forward.npk".{fwd_init, fwd_parse_local_rule, fwd_get_parsed_local_port, fwd_get_parsed_remote_host, fwd_get_parsed_remote_port, fwd_track_local};')

