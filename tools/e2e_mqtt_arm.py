"""Home Assistant over MQTT, and arming and disarming."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)


@case()
def home_assistant(s):
    """Home Assistant through MQTT, against a pretend broker on this PC."""
    def sensor_alert(n):
        get(f'/sensor?{sid(n)}')
    sensor_clear = clear
    settings = get_json('/api/mqtt')
    if settings['host']:
        print('SKIP MQTT: the hub already has a broker set, which these tests would replace')
        return
    broker = FakeBroker(LOCAL)
    serial = get_json('/api/info')['serial']
    base = f'sensors2onvif/{serial}'

    def key(n):
        return SENSORS[n][0].replace('-', '').lower() + f'_{SENSORS[n][1]}'

    def mqtt_page():
        return get_json('/api/mqtt')

    check('a broker given by name is refused', api('POST', '/api/mqtt', 'host=broker.lan&port=1883')[0] == 400)
    check('a broker port out of range is refused', api('POST', '/api/mqtt', f'host={LOCAL}&port=70000')[0] == 400)
    saved = api('POST', '/api/mqtt', f'host={LOCAL}&port={broker.port}&user=hub&password=' + urllib.parse.quote('s3cret pw'))[0]
    check('the MQTT settings can be saved', saved == 200)
    page = poll(mqtt_page, lambda m: m['connected'])
    check('the hub connects to the broker', page['connected'] and page['state'] == 'connected', page)
    check('the page never shows the MQTT password', page['password'] is True and 's3cret' not in json.dumps(page), page)
    hello = broker.connects[0] if broker.connects else {}
    check('the hub logs in with its user and password', hello.get('user') == 'hub' and hello.get('password') == 's3cret pw', hello)
    check('the hub leaves "offline" with the broker as its retained last will', hello.get('will') == (f'{base}/status', b'offline') and hello.get('will_retain'), hello)
    online = wait_until(lambda: broker.latest(f'{base}/status'))
    check('the hub says it is online, retained', online and online['payload'] == b'online' and online['retain'], online)

    # Home Assistant's discovery.
    topic = f'homeassistant/binary_sensor/{serial}/{key(1)}_alert/config'
    told = wait_until(lambda: broker.latest(topic))
    config = json.loads(told['payload']) if told else {}
    check('Home Assistant is told of a sensor, retained',
          told and told['retain'] and config.get('state_topic') == f'{base}/{key(1)}/state' and config.get('availability_topic') == f'{base}/status'
          and config.get('device', {}).get('via_device') == f'sensors2onvif_{serial}', config)
    check('and of every sensor', wait_until(lambda: all(broker.latest(f'homeassistant/event/{serial}/{key(n)}_events/config') for n in SENSORS)))
    health = wait_until(lambda: broker.state(f'{base}/hub/health'))
    if not DEVICE:
        check('the hub\'s health is sent', health and health['free_kb'] == 140 and health['worst_largest_kb'] == 60 and health['rssi'] == -58
              and health['dropped'] >= 0 and health['started'], health)
    else:
        check('the hub\'s health is sent', health and health['free_kb'] > 0 and 'dropped' in health, health)
    told = wait_until(lambda: all(broker.latest(f'homeassistant/sensor/{serial}/hub_{p}/config') for p in ('free_kb', 'worst_largest_kb', 'dropped', 'rssi', 'started')))
    check('and Home Assistant is told of each health sensor', told)
    free = broker.latest(f'homeassistant/sensor/{serial}/hub_free_kb/config')
    free = json.loads(free['payload']) if free else {}
    check('as diagnostic sensors of the hub', free.get('entity_category') == 'diagnostic' and free.get('unit_of_measurement') == 'kB'
          and free.get('state_topic') == f'{base}/hub/health', free)
    hub = wait_until(lambda: broker.latest(f'homeassistant/binary_sensor/{serial}/hub_alert/config'))
    check('and of the hub', hub and json.loads(hub['payload'])['device']['identifiers'] == [f'sensors2onvif_{serial}'], hub)

    # An alert: the sensor's state at once, and the event, which the broker acknowledges.
    sensor_alert(1)
    on = wait_until(lambda: (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'ON')
    check('an alert turns the sensor on in Home Assistant', on, broker.state(f'{base}/{key(1)}/state'))
    check('and the hub', (broker.state(f'{base}/hub/state') or {}).get('alert') == 'ON', broker.state(f'{base}/hub/state'))
    sent = wait_until(lambda: [e for e in broker.events(f'{base}/{key(1)}/event') if json.loads(e['payload'])['event_type'] == 'alert'])
    check('the alert is sent as an event the broker must acknowledge', sent and not sent[-1]['retain'], sent)
    page = poll(mqtt_page, lambda m: m['waiting'] == 0)
    check('the event counts as sent once acknowledged', page['waiting'] == 0, page)
    sensor_clear(1)
    off = wait_until(lambda: (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'OFF', timeout=15)
    check('the alert ending turns the sensor off', off, broker.state(f'{base}/{key(1)}/state'))

    # An event the broker never acknowledged is sent again once the hub has reconnected.
    before = len(broker.events(f'{base}/{key(1)}/event'))
    broker.drop_next_event = True
    sensor_alert(1)
    again = wait_until(lambda: len(broker.events(f'{base}/{key(1)}/event')) >= before + 2, timeout=30)
    events = broker.events(f'{base}/{key(1)}/event')
    check('an event lost with the connection is sent again after reconnecting', again and events[-1]['payload'] == events[-2]['payload'], events[before:])
    check('and the hub logged in again', len(broker.connects) >= 2)
    sensor_clear(1)

    # A sensor added appears in Home Assistant, and one deleted is taken away again.
    path = '02-00-00-00-00-98/1'
    gone_topic = f'homeassistant/binary_sensor/{serial}/020000000098_1_alert/config'
    api('POST', f'/api/mkdir?path={path}')
    added = wait_until(lambda: (broker.latest(gone_topic) or {}).get('payload'))
    check('a sensor added appears in Home Assistant', added)
    api('POST', f'/api/sensor-settings?path={path}', 'name=Back%20door')
    renamed = wait_until(lambda: json.loads(broker.latest(gone_topic)['payload'] or b'{}').get('device', {}).get('name') == 'Back door')
    check('a sensor renamed is renamed in Home Assistant', renamed, broker.latest(gone_topic))
    told = len(broker.messages)
    api('POST', f'/api/sensor-settings?path={path}', 'hold=20')
    time.sleep(0.5)  # time for discovery to be wrongly sent again
    check('a change Home Assistant does not show sends nothing again', len(broker.messages) == told, broker.messages[told:][:3])
    api('DELETE', '/api/file?path=02-00-00-00-00-98')
    removed = wait_until(lambda: broker.latest(gone_topic)['payload'] == b'' and (broker.latest(f'{base}/020000000098_1/state') or {}).get('payload') == b'')
    check('a sensor deleted is taken away from Home Assistant, with its retained state', removed, broker.latest(gone_topic))

    # The alarm panel.
    panel = wait_until(lambda: broker.latest(f'homeassistant/alarm_control_panel/{serial}/hub_alarm/config'))
    panel = json.loads(panel['payload']) if panel else {}
    check('Home Assistant gets an alarm panel', panel.get('command_topic') == f'{base}/armed/set' and panel.get('state_topic') == f'{base}/hub/state'
          and panel.get('payload_disarm') == 'DISARM' and panel.get('payload_arm_away') == 'ARM_AWAY', panel)
    old = broker.latest(f'homeassistant/switch/{serial}/hub_armed/config')
    check('the Armed switch of earlier firmware is taken away', old and old['payload'] == b'' and old['retain'], old)
    check('the hub listens for the panel', (f'{base}/armed/set', 0) in broker.subscriptions, broker.subscriptions)
    check('the panel shows the hub armed', (broker.state(f'{base}/hub/state') or {}).get('alarm') == 'armed_away', broker.state(f'{base}/hub/state'))
    sensor_alert(1)
    check('an alert while armed shows the panel triggered', wait_until(lambda: (broker.state(f'{base}/hub/state') or {}).get('alarm') == 'triggered'))
    sensor_clear(1)
    check('and armed again once it ends', wait_until(lambda: (broker.state(f'{base}/hub/state') or {}).get('alarm') == 'armed_away', timeout=15))
    broker.send(f'{base}/armed/set', b'DISARM', retained=True)
    time.sleep(0.5)  # time for the hub to wrongly obey it
    check('a retained command, repeated by the broker, is ignored', get_json('/api/arm')['armed'] is True)
    # Without a login to the broker, anyone on the network could send the disarm: the hub does not take it then.
    api('POST', '/api/mqtt', f'host={LOCAL}&port={broker.port}&user=&password=')
    wait_until(lambda: mqtt_page()['state'] == 'connected' and len(broker.connects) >= 2 and not broker.connects[-1].get('user'))
    broker.send(f'{base}/armed/set', b'DISARM')
    time.sleep(0.5)  # time for the hub to wrongly obey it
    check('without a login to the broker, a disarm from it is refused', get_json('/api/arm')['armed'] is True)
    check('and the page can say why, as it is told there is no login', mqtt_page().get('user') == '', mqtt_page())
    api('POST', '/api/mqtt', f'host={LOCAL}&port={broker.port}&user=hub&password=' + urllib.parse.quote('s3cret pw'))
    wait_until(lambda: mqtt_page()['state'] == 'connected' and broker.connects[-1].get('user') == 'hub')
    api('POST', '/api/arm/rearm', 'minutes=60')
    broker.send(f'{base}/armed/set', b'DISARM')
    off = poll(lambda: get_json('/api/arm'), lambda a: a['armed'] is False)
    check('Home Assistant can disarm the hub, for the hub\'s re-arm time', off['armed'] is False and off['timed'] and 3500 < (off['left_s'] or 0) <= 3600, off)
    newest = event_list()[0]
    check('disarming is logged, saying from where and for how long', newest['sensor'] == 'hub' and newest['event'] == 'disarmed' and newest['detail'] == 'Home Assistant, for 1 h', newest)
    hub_state = wait_until(lambda: (lambda s: s if s and s.get('alarm') == 'disarmed' else None)(broker.state(f'{base}/hub/state')))
    check('the panel shows the hub disarmed, and when it arms again', hub_state and hub_state.get('rearm_at'), hub_state)
    sensor_alert(1)
    held = wait_until(lambda: [e for e in broker.events(f'{base}/{key(1)}/event') if json.loads(e['payload'])['event_type'] == 'alert while disarmed'])
    check('an alert while disarmed reaches Home Assistant as that event', held)
    check('and the sensor does not turn on', (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'OFF', broker.state(f'{base}/{key(1)}/state'))
    sensor_clear(1)
    broker.send(f'{base}/armed/set', b'ARM_AWAY')
    on = poll(lambda: get_json('/api/arm'), lambda a: a['armed'] is True)
    check('Home Assistant can arm the hub', on['armed'] is True and not on['timed'], on)
    broker.send(f'{base}/armed/set', b'ARM_HOME')
    home = poll(lambda: get_json('/api/arm'), lambda a: a['mode'] == 'home')
    check('Home Assistant can arm the hub home', home['mode'] == 'home', home)
    check('the panel shows it armed home', wait_until(lambda: (broker.state(f'{base}/hub/state') or {}).get('alarm') == 'armed_home'))
    check('the panel offers arming home', 'arm_home' in panel.get('supported_features', []) and panel.get('payload_arm_home') == 'ARM_HOME', panel)
    broker.send(f'{base}/armed/set', b'ARM_AWAY')
    poll(lambda: get_json('/api/arm'), lambda a: a['mode'] == 'away')

    # Each sensor's test button.
    button = wait_until(lambda: broker.latest(f'homeassistant/button/{serial}/{key(1)}_test/config'))
    button = json.loads(button['payload']) if button else {}
    check('Home Assistant gets a test button for each sensor', button.get('command_topic') == f'{base}/{key(1)}/test' and button.get('payload_press') == 'PRESS', button)
    check('the hub listens for the test buttons', (f'{base}/+/test', 0) in broker.subscriptions, broker.subscriptions)
    before = len(broker.events(f'{base}/{key(1)}/event'))
    broker.send(f'{base}/{key(1)}/test', b'PRESS')
    tested = wait_until(lambda: [e for e in broker.events(f'{base}/{key(1)}/event')[before:] if json.loads(e['payload'])['event_type'] == 'test alert'])
    check('pressing it sends a test alert, which comes back as that event', tested)
    check('and the sensor turns on', wait_until(lambda: (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'ON'))
    sensor_clear(1)
    broker.send(f'{base}/armed/set', b'OFF')  # what the switch of earlier firmware sent
    check('the switch of earlier firmware still works', poll(lambda: get_json('/api/arm'), lambda a: a['armed'] is False)['armed'] is False)
    broker.send(f'{base}/armed/set', b'ON')
    poll(lambda: get_json('/api/arm'), lambda a: a['armed'] is True)
    api('POST', '/api/arm/rearm', 'minutes=0')

    # A broker that refuses the login says why on the page.
    broker.refuse = 5
    api('POST', '/api/mqtt', f'host={LOCAL}&port={broker.port}&user=hub&password=')
    refused = poll(mqtt_page, lambda m: 'refused' in m['state'])
    check('a refused login shows on the page', refused['state'] == 'the broker refused: not authorized', refused)
    check('a password left blank keeps the saved one', broker.connects[-1].get('password') == 's3cret pw', broker.connects[-1])
    broker.refuse = 0

    check('the MQTT settings need the web password', api('GET', '/api/mqtt', auth=False)[0] == 401)
    check('MQTT can be turned off', api('POST', '/api/mqtt', 'host=')[0] == 200 and poll(mqtt_page, lambda m: m['state'] == 'not set up')['state'] == 'not set up')
    broker.close()


@case()
def disarmed(s):
    """Disarmed, alerts are logged but not sent to the NVR; one that happened before the disarm still is."""
    check('arming needs the web password', api('POST', '/api/arm', 'armed=0', auth=False)[0] == 401)
    check('a bad arm value is refused', api('POST', '/api/arm', 'armed=maybe')[0] == 400)
    check('the hub can be disarmed', api('POST', '/api/arm', 'armed=0')[0] == 200 and get_json('/api/arm')['armed'] is False)
    newest = event_list()[0]
    check('disarming from the page is logged', newest['event'] == 'disarmed' and newest['detail'] == 'the web page', newest)
    check('/status says the hub is disarmed', status()['armed'] is False)
    get(f'/sensor?{sid(1)}')
    newest = event_list()[0]
    check('an alert while disarmed is logged as that', newest['sensor'] == name(1) and newest['kind'] == 10 and newest['event'] == 'alert while disarmed', newest)
    check('and is not sent to the NVR', name(1) not in status()['active'], status())
    clear(1)
    get(f'/sensor?{sid(2)}&age=500')  # it happened before the hub was disarmed, a moment ago
    check('an alert from before the disarm still reaches the NVR', wait_until(lambda: name(2) in status()['active'], timeout=20), status())
    clear(2)
    get(f'/sensor?{sid(3)}&age=unknown')
    check('so does one whose time is not known', wait_until(lambda: name(3) in status()['active'], timeout=20), status())
    clear(3)
    check('the hub can be armed again', api('POST', '/api/arm', 'armed=1')[0] == 200 and status()['armed'] is True)
    get(f'/sensor?{sid(1)}')
    check('armed, an alert reaches the NVR again', name(1) in status()['active'], status())
    clear(1)


@case()
def a_test_alert(s):
    """A test alert takes the whole path, whatever the arm mode, and is logged as a test."""
    get(f'/sensor?{sid(3)}')  # a sensor heard, for the overview's checklist; a test alert is not one
    clear(3)
    check('a test alert needs the web password', api('POST', f'/api/test-alert?path={name(3)}', 'x=1', auth=False)[0] == 401)
    check('a test alert for no such sensor is refused', api('POST', '/api/test-alert?path=02-00-00-00-00-77/9', 'x=1')[0] == 404)
    api('POST', '/api/arm', 'mode=disarmed&for=0')
    check('a test alert can be sent', api('POST', f'/api/test-alert?path={name(3)}', 'x=1')[0] == 200)
    check('it reaches the NVR even while disarmed', name(3) in status()['active'], status())
    newest = event_list()[0]
    check('and is logged as a test', newest['sensor'] == name(3) and newest['kind'] == 15 and newest['event'] == 'test alert', newest)
    overview = get_json('/api/overview')
    check('the overview shows the alarm, the alert running and the last alert',
          overview['mode'] == 'disarmed' and [a['sensor'] for a in overview['active']] == [name(3)] and overview['last']['event'] == 'test alert'
          and 'listening' in overview['nvr'] and 'mqtt' in overview and isinstance(overview['problems'], list), overview)
    ready = overview.get('ready', {})
    check("the overview's checklist knows the test alert just sent, the clock, the password and a sensor heard",
          ready.get('testAgo_s') is not None and ready['testAgo_s'] < 60 and ready['clock'] and ready['password'] == bool(PASSWORD)
          and ready['heard'] and 'notify' in ready and 'daily' in ready and 'pairedSinceBackup' in ready, ready)
    check('the overview needs the web password', api('GET', '/api/overview', auth=False)[0] == 401)
    clear(3)


@case()
def armed_home(s):
    """Armed home: interior sensors are logged but not sent to the NVR; perimeter ones count as ever."""
    check('a bad zone is refused', api('POST', f'/api/sensor-settings?path={name(2)}', 'zone=garden')[0] == 400)
    check('a sensor can be marked interior', api('POST', f'/api/sensor-settings?path={name(2)}', 'zone=interior')[0] == 200)
    zoned = [s for n in get_json('/api/tree')['nodes'] for s in n['sensors'] if f"{n['mac']}/{s['input']}" == name(2)]
    check('the sensor list shows it interior', zoned and zoned[0]['interior'] is True, zoned)
    check('a bad mode is refused', api('POST', '/api/arm', 'mode=party')[0] == 400)
    check('the hub can be armed home', api('POST', '/api/arm', 'mode=home')[0] == 200 and get_json('/api/arm')['mode'] == 'home')
    newest = event_list()[0]
    check('arming home is logged', newest['event'] == 'armed home' and newest['detail'] == 'the web page', newest)
    check('/status says armed home', status()['mode'] == 'home' and status()['armed'] is True)
    get(f'/sensor?{sid(2)}')
    newest = event_list()[0]
    check('armed home, an interior alert is logged as that', newest['sensor'] == name(2) and newest['kind'] == 13 and newest['event'] == 'alert while armed home', newest)
    check('and is not sent to the NVR', name(2) not in status()['active'], status())
    clear(2)
    get(f'/sensor?{sid(1)}')
    check('armed home, a perimeter alert reaches the NVR', name(1) in status()['active'], status())
    clear(1)
    get(f'/sensor?{sid(2)}&age=500')  # it happened while the hub was armed away
    check('an interior alert from before arming home still reaches the NVR', wait_until(lambda: name(2) in status()['active'], timeout=20), status())
    clear(2)
    api('POST', '/api/arm', 'mode=disarmed&for=0')
    get(f'/sensor?{sid(1)}&age=3')  # it happened while armed home, a moment before the disarm
    check('a perimeter alert from before the disarm, while armed home, still reaches the NVR', wait_until(lambda: name(1) in status()['active'], timeout=20), status())
    clear(1)
    api('POST', '/api/arm', 'mode=away')
    api('POST', f'/api/sensor-settings?path={name(2)}', 'zone=perimeter')


@case(pc=True)
def a_storage_error_keeps_the_sensors(s):
    """A storage error while the sensor table is rebuilt must not make the hub forget its sensors: their alerts would be
    ignored as from unknown senders until the next change."""
    control('lists-fail')
    changed = api('POST', f'/api/sensor-settings?path={name(1)}', 'hold=20')[0]  # a change, so the table is rebuilt
    code = get(f'/sensor?{sid(1)}')[0]
    control('lists-work')
    check('a folder that cannot be read keeps its sensors', changed == 200 and code == 200 and name(1) in status()['active'], (changed, code, status()))
    clear(1)
    api('POST', f'/api/sensor-settings?path={name(1)}', 'hold=')
    control('stats-fail')  # likewise a folder whose entries cannot be looked at, as when memory runs out
    changed = api('POST', f'/api/node-settings?path={SENSORS[3][0]}', 'late=300')[0]
    code = get(f'/sensor?{sid(3)}')[0]
    control('stats-work')
    check('a folder whose entries cannot be looked at keeps its sensors', changed == 200 and code == 200 and name(3) in status()['active'],
          (changed, code, status()))
    clear(3)
    api('POST', f'/api/node-settings?path={SENSORS[3][0]}', '')


@case()
def a_disarm_that_ends_by_itself(s):
    """A disarm that ends by itself, so forgetting to arm again cannot leave the house unwatched."""
    check('a disarm longer than a week is refused', api('POST', '/api/arm', 'armed=0&for=10081')[0] == 400)
    check('a bad re-arm time is refused', api('POST', '/api/arm/rearm', 'minutes=soon')[0] == 400)
    check('the re-arm time for Home Assistant can be saved', api('POST', '/api/arm/rearm', 'minutes=480')[0] == 200 and get_json('/api/arm')['rearm'] == 480)
    api('POST', '/api/arm/rearm', 'minutes=0')
    if not DEVICE:
        control('minutes-fast')  # a minute lasts a second
        api('POST', '/api/arm', 'armed=0&for=2')
        timed = get_json('/api/arm')
        check('a disarm can be for a time', timed['armed'] is False and timed['timed'] and 0 < timed['left_s'] <= 120, timed)
        check('disarming for a time is logged with it', event_list()[0]['detail'] == 'the web page, for 2 min', event_list()[0])
        check('the hub arms itself again when the time is up', wait_until(lambda: get_json('/api/arm')['armed'], timeout=10))
        newest = event_list()[0]
        check('arming by the timer is logged', newest['event'] == 'armed' and newest['detail'] == 'the re-arm timer', newest)
        get(f'/sensor?{sid(1)}')
        check('and alerts reach the NVR again', name(1) in status()['active'], status())
        clear(1)
        api('POST', '/api/arm', 'armed=0&for=1')
        api('POST', '/api/arm', 'armed=0&for=0')  # changed to until armed again
        time.sleep(2)  # longer than the first time given
        check('a disarm changed to last until armed again does not end by itself', get_json('/api/arm')['armed'] is False)
        api('POST', '/api/arm', 'armed=1')
