import re

def replace_in_file(file_path, old, new):
    with open(file_path, 'r') as file:
        data = file.read()
    data = data.replace(old, new)
    with open(file_path, 'w') as file:
        file.write(data)

# 1. auth_dialog.npk: add pub to AUTH_DIALOG_DONE and AUTH_DIALOG_BUSY
replace_in_file("src/ssh/auth_dialog.npk", "func:AUTH_DIALOG_DONE    = int32", "pub func:AUTH_DIALOG_DONE    = int32")
replace_in_file("src/ssh/auth_dialog.npk", "func:AUTH_DIALOG_BUSY    = int32", "pub func:AUTH_DIALOG_BUSY    = int32")

# 2. test_ssh_session.npk: fix ()()
replace_in_file("src/ssh/ssh_session.npk", "AUTH_DIALOG_DONE()()", "AUTH_DIALOG_DONE()")

# 3. test_ssh_session.npk: line 83 and 85, etc
# Actually, the error was: tests/ssh/test_ssh_session.npk:0:0: error: Line 83, Column 5: Cannot silently unwrap Result<int32> into 'result' of type 'int32'.
# But earlier we found that it was in auth_dialog.npk:83, and I ran:
# fix("src/ssh/auth_dialog.npk", 83, "int32:result = auth_try_next(p);", "int32:result = raw auth_try_next(p);")
# Let's check test_ssh_session.npk lines 85 and 90, maybe it was in tests/ssh/test_ssh_session.npk?
# In test_ssh_session.npk:
# 82: int64:got_sess = raw pane_get_ssh_session(slot);
# 83: drop(check_int("...", 0xDEADBEEFi64, got_sess));
# 87: int64:got_chan = raw pane_get_ssh_channel(slot);
# 88: drop(check_int("...", 7i64, got_chan));
# 91: int64:is_ssh = raw pane_is_ssh(slot);
# 92: drop(check_int("...", 1i64, is_ssh));

# The error: "tests/ssh/test_ssh_session.npk:0:0: error: Line 83, Column 5: Cannot silently unwrap Result<int32> into 'result' of type 'int32'."
# Wait, look at test_ssh_session.npk line 83. It is drop(check_int(...)). There is NO 'result'!
# So it MUST be from ANOTHER file that has `int32:result = auth_try_next(p);` on line 83. That IS auth_dialog.npk!
# But wait, did the compiler report multiple "result" errors?
# "Line 85, Column 9: NITPICK-RESULT-OUT-OF-SCOPE: 'result' is only available inside ensures clauses"
# "Line 85, Column 33: Cannot call non-function type 'int32'"
# "Line 90, Column 9: NITPICK-RESULT-OUT-OF-SCOPE: 'result' is only available inside ensures clauses"
# "Line 90, Column 35: Cannot call non-function type 'int32'"
# "Line 95, Column 9: NITPICK-RESULT-OUT-OF-SCOPE: 'result' is only available inside ensures clauses"
# "Line 95, Column 41: Cannot call non-function type 'int32'"

# Where are these "result" lines?
# Let's look at auth_dialog.npk:
# 83:     int32:result = raw auth_try_next(p);
# 85:     if (result == AUTH_RESULT_OK()) {
# 90:     if (result == AUTH_RESULT_FAIL()) {
# 95:     if (result == AUTH_RESULT_NEED_INPUT()) {
# YES!!! auth_dialog.npk lines 85, 90, 95!!!
# WHY does it complain about "result is only available inside ensures clauses"?!
# Because 'result' is a RESERVED KEYWORD in Nitpick used for post-conditions!!!
# Ah!!! They named the variable 'result'!
replace_in_file("src/ssh/auth_dialog.npk", "int32:result =", "int32:res =")
replace_in_file("src/ssh/auth_dialog.npk", "if (result ==", "if (res ==")

# 4. ssh_session.npk line 174: Cannot compare int64 to Result<int64> directly. Use 'raw' to unwrap
# Let's read ssh_session.npk around 174.
