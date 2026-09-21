# Time sync: chrony on Home Assistant

The effects keep a room in step by reading the clock, so every light has to agree on the time.
The easiest way is to run an NTP server on Home Assistant itself and point every light at it:

- **One source for every light.** Lights that sync to different internet servers can disagree by
  tens of milliseconds; lights that sync to the same local server agree far more closely.
- **It keeps working if the internet drops.** The lights keep syncing to Home Assistant, and chrony
  keeps its own clock steady until the upstream servers are back.
- **The lights need no internet access.** Only Home Assistant talks to the public NTP pool, which
  suits an IoT VLAN with outbound traffic blocked.

## 1. Install the chrony app

1. **Settings → Apps → App store** (called Add-ons before 2026.2) and search for **chrony**. It
   is in the Home Assistant Community Apps repository, which HA includes by default.
2. Install it, and turn on **Start on boot** and **Watchdog**.

## 2. Configure it

Open the app's **Configuration** tab:

```yaml
mode: pool
ntp_pool: pool.ntp.org
set_system_clock: false
```

| Option | Use |
| --- | --- |
| `mode` | `pool` to follow a pool of public servers (recommended); `server` to list specific servers in `ntp_server`. |
| `ntp_pool` | `pool.ntp.org`, or your country's zone for nearer servers: `us.pool.ntp.org`, `uk.pool.ntp.org`, `de.pool.ntp.org`… |
| `ntp_server` | With `mode: server`: a list, e.g. `time.cloudflare.com`, `time.google.com`, or a stratum-1 server on your network. |
| `set_system_clock` | Leave **false**. Home Assistant OS already keeps its own clock in sync; this app is only there to serve time to the network. |

In the **Network** section, keep port **123/udp** mapped to 123. That is the port the lights will
use. Start (or restart) the app.

Check the app's **Log** tab. After a minute or so it should list pool servers as sources, with one
selected.

## 3. Point the lights at it

In each light's config, or in a shared package, set the first NTP server to Home Assistant's IP
address. Use the host's LAN address, not a hostname: the lights are asking for time before
anything else is up.

```yaml
substitutions:
  ntp_server_1: 192.168.1.10          # Home Assistant
  ntp_server_2: pool.ntp.org          # fallbacks, only if HA doesn't answer
  ntp_server_3: time.cloudflare.com
```

`esphome/common/ntp.yaml` uses these three substitutions and polls every 5 minutes.

## 4. Check it

Each light has two sensors from `ntp.yaml`:

- **NTP Sync**: on while the light's last successful sync is under 15 minutes old.
- **Last NTP Sync**: when that was, which Home Assistant shows as, for example, "3 minutes ago".

A quick visual test is the **Metronome** effect on a whole room: every light should tick together,
on the second.

## If a room drifts apart

1. **Check NTP Sync first.** If it is off on every light, the lights can't reach the server.
2. **Check the firewall.** The usual cause is a rule blocking **UDP 123** from the lights'
   network or VLAN to Home Assistant. Allow it. The lights don't need internet NTP at all.
3. **Reboot the lights once the path is open.** ESPHome syncs at boot, so that snaps them back
   straight away instead of waiting for the next 5-minute poll. Reboot a few at a time.
4. **Don't also run the `homeassistant` time platform on the lights.** It sets whole seconds only,
   and fights SNTP.
