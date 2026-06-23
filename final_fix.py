import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

# ssh_session.npk:174: if (kh_res == raw KH_MISMATCH()) {
replace_in_file("src/ssh/ssh_session.npk", "if (kh_res == KH_MISMATCH()) {", "if (kh_res == raw KH_MISMATCH()) {")

