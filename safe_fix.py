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

# 1. src/ssh/ssh_session.npk
fix("src/ssh/ssh_session.npk", 34, "{env_get}", "{get}")
fix("src/ssh/ssh_session.npk", 171, "raw nitpick_ssh_decode_string_data", "nitpick_ssh_decode_string_data")
fix("src/ssh/ssh_session.npk", 259, "int32:tick_result = auth_dialog_tick", "int32:tick_result = raw auth_dialog_tick")
# Wait, for 348 'SSH_compression_get', I'll comment it out or change it, but let me read what it should be first.
fix("src/ssh/ssh_session.npk", 432, "int32:recv_chan = nitpick", "int64:recv_chan = nitpick")
fix("src/ssh/ssh_session.npk", 433, "recv_chan64 = @cast_unchecked<int64>(recv_chan)", "recv_chan64 = recv_chan")
fix("src/ssh/ssh_session.npk", 469, "int32:confirm_our_chan = nitpick", "int64:confirm_our_chan = nitpick")
fix("src/ssh/ssh_session.npk", 470, "confirm_chan64 = @cast_unchecked<int64>(confirm_our_chan)", "confirm_chan64 = confirm_our_chan")
fix("src/ssh/ssh_session.npk", 504, "int32:bind_port32 = nitpick", "int64:bind_port32 = nitpick")
fix("src/ssh/ssh_session.npk", 505, "bind_port = @cast_unchecked<int64>(bind_port32)", "bind_port = bind_port32")
fix("src/ssh/ssh_session.npk", 518, "int32:closed_chan32 = nitpick", "int64:closed_chan32 = nitpick")
fix("src/ssh/ssh_session.npk", 519, "closed_chan = @cast_unchecked<int64>(closed_chan32)", "closed_chan = closed_chan32")
fix("src/ssh/ssh_session.npk", 763, "int32:tick_result2 = auth_dialog_tick", "int32:tick_result2 = raw auth_dialog_tick")

# 2. src/ssh/ssh_x11.npk
fix("src/ssh/ssh_x11.npk", 89, "raw nitty_x11_get_display", "nitty_x11_get_display")
fix("src/ssh/ssh_x11.npk", 95, "string:cookie = x11_gen_fake_cookie", "string:cookie = raw x11_gen_fake_cookie")
fix("src/ssh/ssh_x11.npk", 118, "SSH_buf_write_u32", "nitpick_ssh_buf_write_uint32")
fix("src/ssh/ssh_x11.npk", 124, "SSH_buf_write_u32", "nitpick_ssh_buf_write_uint32")
fix("src/ssh/ssh_x11.npk", 133, "int64:slot = x11_alloc_slot", "int64:slot = raw x11_alloc_slot")
fix("src/ssh/ssh_x11.npk", 149, "raw nitty_x11_get_display", "nitty_x11_get_display")
fix("src/ssh/ssh_x11.npk", 156, "int64:slot = x11_alloc_slot", "int64:slot = raw x11_alloc_slot")
fix("src/ssh/ssh_x11.npk", 183, "raw SSH_channel_read(sess, chan_id, X11_BUF_SIZE)", "raw SSH_channel_read(sess, chan_id)")
fix("src/ssh/ssh_x11.npk", 196, "raw nitty_x11_read", "nitty_x11_read")
fix("src/ssh/ssh_x11.npk", 208, "raw nitty_x11_close", "nitty_x11_close")
fix("src/ssh/ssh_x11.npk", 230, "raw nitty_x11_close", "nitty_x11_close")

# 3. src/ssh/ssh_forward.npk
fix("src/ssh/ssh_forward.npk", 572, "string:local_rules = profile_get_local_forwards", "string:local_rules = raw profile_get_local_forwards")

# 4. src/ssh/auth_dialog.npk
# Wait, was the error for 'meth' at auth_dialog.npk:82 ? Yes!
fix("src/ssh/auth_dialog.npk", 82, "int32:meth = auth_current_method", "int32:meth = raw auth_current_method")

