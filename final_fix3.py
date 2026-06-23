import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

replace_in_file("src/ssh/ssh_session.npk", "if (tick_result == AUTH_DIALOG_DONE())", "if (tick_result == raw AUTH_DIALOG_DONE())")
replace_in_file("src/ssh/ssh_session.npk", "if (tick_result2 == AUTH_DIALOG_DONE())", "if (tick_result2 == raw AUTH_DIALOG_DONE())")

replace_in_file("src/ssh/ssh_session.npk", "SSH_compression_get", "ssh_compression_get")
replace_in_file("src/ssh/ssh_session.npk", "SSH_compression_set", "ssh_compression_set")

