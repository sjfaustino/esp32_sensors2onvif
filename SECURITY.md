# Security

The hub guards homes, so a weakness in it can let an intruder go unrecorded. Please report one privately, not in a
public issue.

## Reporting a vulnerability

Use GitHub's private reporting: on this repository's **Security** tab, choose **Report a vulnerability**. Only the
maintainer sees the report. Please include:

- the firmware version, from **Device Info** on the hub's page, and the board;
- what someone could do, and from where: on the same Wi-Fi network, within radio range of the hub or a node, or with
  the hub or a node in their hands;
- the steps to reproduce it, as short as you can make them.

You will get an answer as soon as possible. A fix comes in a new release, and the report is published once hubs can be
updated, with credit to you if you would like it.

## Supported versions

Only the newest release gets security fixes. Hubs update from their own web page, under **System Manager >
Maintenance**.

## Known limits

These are part of the design and documented in the README, so they need no report:

- HTTP, ONVIF, RTSP and MQTT on the local network are not encrypted, as is usual between local devices. Anyone who can
  watch the network can read the hub password.
- Passwords and keys are kept on the hub's flash unencrypted: someone who takes the hub can read them.
- Someone who records a node's message can play it back later. That makes a false alert, never a false all-clear; a
  paired node also cannot be kept looking alive that way.
- Anyone who can publish to the MQTT broker can arm and disarm the hub through Home Assistant: give the broker a login.
- A radio jammer can keep nodes from reaching the hub. The hub then reports them silent, and several going quiet
  together as one event, but cannot stop the jamming.

How the hub keeps its promises, and where, is in [docs/how-it-works.md](docs/how-it-works.md).
