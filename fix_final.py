import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

# 1. auth_dialog.npk: remove () from AUTH_RESULT_*
replace_in_file("src/ssh/auth_dialog.npk", "if (res == AUTH_RESULT_OK()) {", "if (res == AUTH_RESULT_OK) {")
replace_in_file("src/ssh/auth_dialog.npk", "if (res == AUTH_RESULT_FAIL()) {", "if (res == AUTH_RESULT_FAIL) {")
replace_in_file("src/ssh/auth_dialog.npk", "if (res == AUTH_RESULT_NEED_INPUT()) {", "if (res == AUTH_RESULT_NEED_INPUT) {")

# 2. ssh_forward.npk: fix fwd_parse_local_rule and fwd_local_start
replace_in_file("src/ssh/ssh_forward.npk", "drop(fwd_parse_local_rule(rule));", "drop(fwd_parse_local_rule(rule, 0i64, \"\", 0i64));")
replace_in_file("src/ssh/ssh_forward.npk", "drop(fwd_local_start(pane_slot, session, g_fwd_parse_local_port, g_fwd_parse_remote_host, g_fwd_parse_remote_port));", "drop(fwd_local_start(pane_slot, g_fwd_parse_local_port, g_fwd_parse_remote_host, g_fwd_parse_remote_port));")

# 3. ssh_forward.npk: add string_find before fwd_apply_profile_rules
find_func = """func:string_find = int64(string:s, string:sub, int64:start) {
    int64:len = string_length(s);
    int64:sub_len = string_length(sub);
    if (sub_len == 0i64) { pass(start); }
    int64:i = start;
    while (i <= len - sub_len) {
        if (string_substring(s, i, i + sub_len) == sub) {
            pass(i);
        }
        i = i + 1i64;
    }
    pass(-1i64);
};

/// Apply forwarding rules from a connection profile after successful SSH auth."""

replace_in_file("src/ssh/ssh_forward.npk", "/// Apply forwarding rules from a connection profile after successful SSH auth.", find_func)

