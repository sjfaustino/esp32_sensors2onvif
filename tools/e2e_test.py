"""End-to-end test of the firmware library, built for the PC (pio run -e host).

Plays the NVR's part using the call order recorded from the real NVR. By default every socket is
loopback. Needs ffmpeg.

Each test stands on its own: it starts from a quiet hub with the test sensors in place (e2e_common.quiet_hub), sets up
what else it needs, and puts back what it changed. So any of them can run alone:

    py tools/e2e_test.py                  every test
    py tools/e2e_test.py node webhook     the tests with "node" or "webhook" in their names
    py tools/e2e_test.py --list           their names

To test a flashed board instead, restart it first, then set E2E_DEVICE to its address and
E2E_LOCAL_IP to this PC's address on the same network. If the board has a password, set
E2E_WEB_PASSWORD to it and E2E_SENSOR_TOKEN to the token on its HTTP page. It creates and deletes only the
02-00-00-00-xx-xx test folders. If the board has its own clear.h264 or text.json it skips the tests that would replace
them, and it stops if it cannot tell. The tests that need the PC build's test commands are skipped. Never point
E2E_DEVICE at an NVR.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the parts beside this file
from e2e_common import *  # noqa: E402,F401,F403
# Each part registers its tests as it is read, in this order.
import e2e_files, e2e_nvr, e2e_sensors, e2e_mqtt_arm, e2e_nodes  # noqa: E402,F401


def main():
    words = main_of(__doc__.splitlines()[0])
    print('making test pictures', flush=True)
    files = make_frames.sensor_files('TEST')
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage:
        session = Session(storage, files)
        try:
            failed = run_cases(session, words)
        finally:
            session.stop()
    finish(failed)


if __name__ == '__main__':
    main()
