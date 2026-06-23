import re

def fix(file, line, old, new):
    with open(file, 'r') as f:
        lines = f.readlines()
    if old in lines[line-1]:
        lines[line-1] = lines[line-1].replace(old, new)
        with open(file, 'w') as f:
            f.writelines(lines)
        print(f"Fixed {file}:{line}")
    else:
        print(f"ERROR: {old} not found on {file}:{line} (Found: {lines[line-1].strip()})")

# 1. auth_dialog.npk:83 -> int32:result = raw auth_try_next(p);
fix("src/ssh/auth_dialog.npk", 83, "int32:result = auth_try_next(p);", "int32:result = raw auth_try_next(p);")

# 2. ssh_forward.npk:572 -> string:local_rules = raw profile_get_local_fwd(prof_slot);
fix("src/ssh/ssh_forward.npk", 572, "string:local_rules = profile_get_local_fwd(prof_slot);", "string:local_rules = raw profile_get_local_fwd(prof_slot);")

# 3. ssh_x11.npk:118 -> nitpick_ssh_encode_uint32
# Wait, let's look at what the function was before. "SSH_buf_write_u32". But I changed it to "nitpick_ssh_buf_write_uint32".
# Wait, I previously did:
# fix("src/ssh/ssh_x11.npk", 118, "SSH_buf_write_u32", "nitpick_ssh_buf_write_uint32")
# And the compiler error said: Undefined identifier: 'nitpick_ssh_buf_write_uint32'. Did you mean 'nitpick_ssh_buf_write_byte'?
# I need to change it to 'ssh_encode_uint32'!
fix("src/ssh/ssh_x11.npk", 118, "nitpick_ssh_buf_write_uint32", "ssh_encode_uint32")
fix("src/ssh/ssh_x11.npk", 124, "nitpick_ssh_buf_write_uint32", "ssh_encode_uint32")

# 4. ssh_x11.npk:152: 'raw' argument must be Result<T> — got 'int64'.
# 152: int64:local_fd = raw nitty_x11_connect_display(display);
fix("src/ssh/ssh_x11.npk", 152, "int64:local_fd = raw nitty_x11_connect_display", "int64:local_fd = nitty_x11_connect_display")

# 5. ssh_x11.npk:185: 'raw' argument must be Result<T> — got 'int64'.
# 185: int64:wrote = raw nitty_x11_write(
fix("src/ssh/ssh_x11.npk", 185, "int64:wrote = raw nitty_x11_write", "int64:wrote = nitty_x11_write")

# 6. ssh_session.npk:174: Cannot compare int64 to Result<int64> directly.
# 174: if (kh_res == ssh_kh_mismatch_warning("...")) ? No, let's just see 174!
# Wait, what was line 174 in ssh_session.npk? I don't know EXACTLY. Let's just find "kh_res" around 174 and put 'raw'.
# It is better to use replace_file_content for that. I will skip 174 for the python script for now, and see it!

# 7. ssh_session.npk:348: 'SSH_compression_get' -> 'ssh_compression_get'
fix("src/ssh/ssh_session.npk", 348, "SSH_compression_get", "ssh_compression_get")

# 8. test_ssh_session.npk:260: Undefined identifier: 'AUTH_DIALOG_DONE'
# Wait! AUTH_DIALOG_DONE is in `src/ssh/auth_dialog.npk`. If `test_ssh_session.npk` calls it... wait, does it?
# In `ssh_session.npk` line 260, it says: `if (tick_result == AUTH_DIALOG_DONE())`
# But it imports `auth_dialog.npk`?
# Let's add `{AUTH_DIALOG_DONE}` to imports or something? Wait, does it use it as `AUTH_DIALOG_DONE()`?
fix("src/ssh/ssh_session.npk", 260, "AUTH_DIALOG_DONE", "AUTH_DIALOG_DONE()")

