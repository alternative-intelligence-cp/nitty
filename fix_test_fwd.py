import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

imports = 'use "../../src/config/profile.npk".{profile_get_remote_fwd, profile_get_dynamic_port, profile_get_jump_host, profile_get_jump_port, profile_get_jump_user, profile_has_jump, profile_set_forwarding, profile_set_jump, profiles_init};'

replace_in_file("tests/ssh/test_ssh_forward.npk", 'use "../../src/config/profile.npk".*;', imports)
replace_in_file("tests/ssh/test_ssh_forward.npk", "func:main = int32() {", "func:failsafe = int32(tbb32:err) { exit 1i32; };\nfunc:main = int32() {")

