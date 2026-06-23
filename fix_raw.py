import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

# Fix ssh_session.npk lines 262 and 766
replace_in_file("src/ssh/ssh_session.npk", "if (auth_dialog_success(pane_slot) == 1i32) {", "if (raw auth_dialog_success(pane_slot) == 1i32) {")

# Fix ssh_x11.npk encode_uint32
replace_in_file("src/ssh/ssh_x11.npk", "ssh_encode_uint32", "SSH_encode_uint32")

