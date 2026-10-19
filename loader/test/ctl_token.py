"""Test control channel token (v0.1.16, src/rig_sock.c "channel guard").

When the channel opens, the game writes a fresh random token (32 hex characters, 128 bits) to <rig dir>/ctl.token.
Every connection must send that token, bare, as its FIRST line; the commands follow, one JSON object per line, as
before. The game does not answer the token line. A wrong token gets one error line, then the connection is closed.

Read the file AFTER the connection is made: the game writes it before it listens, so it is then the current one.
No file (a build before v0.1.16): nothing is sent, and the old build works as before."""
import os


def token_line(rig_dir):
    """the bytes to send first on a new connection to the copy whose DSRIG_DIR is rig_dir ('' when there is no token)"""
    try:
        with open(os.path.join(rig_dir, 'ctl.token')) as f:
            t = f.read().strip()
    except OSError:
        return b''
    return (t + '\n').encode() if t else b''
