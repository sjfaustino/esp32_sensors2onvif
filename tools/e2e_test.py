"""End-to-end test of the firmware library, built for the PC (pio run -e host).

Plays the NVR's part using the call order recorded from the real NVR. By default every socket is
loopback. Needs ffmpeg.

To test a flashed board instead, restart it first, then set E2E_DEVICE to its address and
E2E_LOCAL_IP to this PC's address on the same network. If the board has a password, set
E2E_WEB_PASSWORD to it and E2E_SENSOR_TOKEN to the token on its HTTP page. It creates and deletes only the 02-00-00-00-00-0x test folders. If the board
has its own clear.h264 or text.json it skips the checks that would replace them, and it stops
if it cannot tell. Never point E2E_DEVICE at an NVR.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the parts beside this file
from e2e_common import *  # noqa: E402,F401,F403
import e2e_common as common  # noqa: E402
from e2e_mqtt_arm import *  # noqa: E402,F401,F403
from e2e_nodes import *  # noqa: E402,F401,F403
from e2e_files import *  # noqa: E402,F401,F403
from e2e_sensors import *  # noqa: E402,F401,F403
from e2e_nvr import *  # noqa: E402,F401,F403


def main():
    print('making test pictures', flush=True)
    files = make_frames.sensor_files('TEST')
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage:
        proc = None if DEVICE else start_hub(storage, PASSWORD)
        try:
            if proc:
                check('server starts', common.HUB.stdout.readline().strip() == 'ready')
            # An NVR is listening before any alert, as in use. Without one, the hub would keep every alert for a replay.
            hub_files_free, puller = test_start_with_an_nvr_listening(files)

            # The order the NVR used when first connecting.
            code, data, device, path = test_nvr_first_connection_order()

            # The NVR sends Subscribe to device_service, twice, with one listener URL.
            code, colour, data, nvr = test_nvr_subscribe(device, files, hub_files_free)

            # A sensor alert makes a motion event, and the picture shows it. Sensor 3 has no files.
            before, code, colour, data, started = test_sensor_alert_makes_a_motion_event(nvr)

            # Repeated "true" while active, then "false" when the hold ends.
            colour = test_alert_repeats_then_ends(before, hub_files_free, nvr, started)

            # A clear report does not end the alert early, so activations close together make one NVR record.
            before, started = test_clear_report_does_not_end(nvr)
            # Waited for rather than slept: over a slow network the last activation lands late, and its hold ends later.
            wait_until(lambda: 'false' in nvr.states()[before:], timeout=HOLD_MS / 1000 * 0.4 + 6)
            check('the alert ends when the hold after the last activation runs out', nvr.states()[before:][-1:] == ['false'] and nvr.states()[before:].count('false') == 1, nvr.states()[before:])

            # The Live page shows the picture being sent. The picture changes on the next frame after the alert ends.
            code, firing, idle = test_live_page()

            # The Nodes and pairing page lists every sensor node.
            before, c, code, events, gone, idle, quiet, received, reply, s, seen = test_esp_now_page()

            # Other clients may poll instead.
            code, data = soap('CreatePullPointSubscription', '/onvif/events')
            address = re.search(r'/onvif/sub/\d+', data).group(0)
            check('pull point delivers the state once', 'Value="false"' in soap('PullMessages', address)[1])
            check('pull point is quiet until a change', 'NotificationMessage' not in soap('PullMessages', address)[1])

            # A poll with a timeout waits for news, instead of being answered at once.
            test_poll_timeout_waits_for_news(address)
            test_subscription_is_its_makers(address)

            # WS-Discovery, sent directly.
            test_ws_discovery()

            # A slightly old report still counts as live.
            now = test_slightly_old_report_still_counts()

            # Cached alerts: amber picture, one record each, true time kept in the event list.
            before, colour = test_cached_alerts(nvr)

            # Each node sets how old an alert must be to count as a past event. Sensor 3 has no pictures.
            test_node_past_event_time(s)

            # A listener that accepts and never answers must not stall the camera or the good subscriber.
            test_hung_listener(device, nvr)

            # Web connections and pushes share a few sockets, so a push may have to wait for one. It must still go out.
            test_pushes_wait_for_sockets(device, nvr, proc)

            # The limit of 32 is refused up front. Everything made here is under a node this test owns, and is removed again.
            test_sensor_limit()

            # A delayed alert belongs to its device. A new sensor that takes a deleted one's place must not play it.
            gone, newcomer = '02-00-00-00-44-01', '02-00-00-00-44-02'
            api('POST', f'/api/mkdir?path={gone}/1')
            # Past events from earlier checks play out first, so none is playing: a live alert would send it back to the queue.
            shown, watched = test_deleted_sensor_delayed_alert(gone, newcomer)

            # Until the queue is empty, which may first wait out an earlier past event's gap, then through the time it
            end = test_deleted_sensor_delayed_alert_not_played(newcomer, shown, watched)

            # Late alerts from one sensor close together are one record, as they would have been live.
            test_late_alerts_from_one_sensor()

            # Deleting folders removes the sensors, without a reboot.
            proc = test_deleting_folders_and_restart(files, proc, puller, storage)
        finally:
            if proc:
                proc.kill()
                proc.wait()
    print(f'\n{len(failures)} failure(s)' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
