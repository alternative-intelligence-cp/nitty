import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

# Fix AUTH_METHOD_KBDINT() and AUTH_METHOD_PASSWORD()
replace_in_file("src/ssh/auth_dialog.npk", "if (meth == AUTH_METHOD_KBDINT()) {", "if (meth == AUTH_METHOD_KBDINT) {")
replace_in_file("src/ssh/auth_dialog.npk", "if (meth == AUTH_METHOD_PASSWORD()) {", "if (meth == AUTH_METHOD_PASSWORD) {")

# Fix _fwd_string_find returning Result<int64>
replace_in_file("src/ssh/ssh_forward.npk", "_fwd_string_find(local_rules, \",\", start_idx)", "raw _fwd_string_find(local_rules, \",\", start_idx)")

