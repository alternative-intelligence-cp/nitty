import os

with open('/home/randy/Workspace/REPOS/nitty/tests/ssh/test_ssh_e2e.npk', 'r') as f:
    c = f.read()
# zmodem_state returns int64, so remove raw
c = c.replace('raw zmodem_state', 'zmodem_state')
with open('/home/randy/Workspace/REPOS/nitty/tests/ssh/test_ssh_e2e.npk', 'w') as f:
    f.write(c)

with open('/home/randy/Workspace/REPOS/nitty/src/ssh/ssh_forward.npk', 'r') as f:
    c = f.read()
# _fwd_string_find requires raw? Or wait, if _fwd_string_find returns int64 according to its signature... why does the compiler think it returns Result? Let's just add raw.
c = c.replace('= _fwd_string_find', '= raw _fwd_string_find')
with open('/home/randy/Workspace/REPOS/nitty/src/ssh/ssh_forward.npk', 'w') as f:
    f.write(c)

