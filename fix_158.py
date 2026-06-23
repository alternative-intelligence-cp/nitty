import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

replace_in_file("src/ssh/ssh_x11.npk", "drop(raw nitty_x11_close(@cast_unchecked<int32>(local_fd)));", "drop(nitty_x11_close(@cast_unchecked<int32>(local_fd)));")

