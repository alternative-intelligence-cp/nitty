import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

replace_in_file("src/ssh/ssh_forward.npk", "func:string_find =", "func:_fwd_string_find =")
replace_in_file("src/ssh/ssh_forward.npk", "string_find(local_rules, \",\", start_idx)", "_fwd_string_find(local_rules, \",\", start_idx)")

