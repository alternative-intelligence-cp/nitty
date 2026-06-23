import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

replace_in_file("tests/ssh/test_ssh_session.npk", "func:failsafe = int32(tbb32:err) { pass 1i32; };", "func:failsafe = int32(tbb32:err) { exit 1i32; };")

