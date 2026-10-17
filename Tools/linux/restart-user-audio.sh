#!/bin/sh
# Run as root after resume from suspend/hibernate (see restart-user-audio.service).
# After a resume PipeWire can come back with every sound card profile "off" (only "Dummy Output"), and
# Proton games then hang in DirectSound start-up. For each logged-in user whose audio server has no real
# default sink, restart WirePlumber, and if that is not enough, the whole PipeWire stack.

TAG=restart-user-audio
log() { logger -t "$TAG" -- "$*"; }

[ "$(id -u)" = 0 ] || { echo "$TAG: must run as root (it is started by systemd)" >&2; exit 1; }

# Runs a command as the user, inside their systemd user session.
as_user() {
    user=$1; uid=$2; shift 2
    runuser -u "$user" -- env XDG_RUNTIME_DIR="/run/user/$uid" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$uid/bus" "$@"
}

has_output() {
    sink=$(as_user "$1" "$2" pactl get-default-sink 2>/dev/null)
    [ -n "$sink" ] && [ "$sink" != "auto_null" ]
}

# Wait up to ~10 s for the sink to appear.
wait_for_output() {
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        has_output "$1" "$2" && return 0
        sleep 1
    done
    return 1
}

sleep 3  # let the sound cards come back first

for uid in $(ls /run/user 2>/dev/null); do
    [ "$uid" -ge 1000 ] 2>/dev/null || continue
    [ -S "/run/user/$uid/bus" ] || continue
    user=$(id -nu "$uid" 2>/dev/null) || continue

    if wait_for_output "$user" "$uid"; then
        continue
    fi
    log "no audio output for $user after resume: restarting wireplumber"
    as_user "$user" "$uid" systemctl --user restart wireplumber
    if wait_for_output "$user" "$uid"; then
        log "audio output back for $user"
        continue
    fi
    log "still no audio output for $user: restarting pipewire, pipewire-pulse, wireplumber"
    as_user "$user" "$uid" systemctl --user restart pipewire pipewire-pulse wireplumber
    if wait_for_output "$user" "$uid"; then
        log "audio output back for $user"
    else
        log "audio output still missing for $user"
    fi
done
exit 0
