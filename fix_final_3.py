import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

replace_in_file("src/ssh/auth_dialog.npk", "string:user_nm = auth_last_error(p);", "string:user_nm = raw auth_last_error(p);")

