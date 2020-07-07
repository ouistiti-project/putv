#!/bin/sh
#
# Starts upnprender.
#

DAEMON=upnprender
PIDFILE="/var/run/$DAEMON.pid"
WEBSOCKETDIR="/var/run/websocket"

OPTIONS="-o putv"
OPTIONS="${OPTIONS} -I 192.168.10.254"

[ -r "/etc/default/$DAEMON" ] && . "/etc/default/$DAEMON"

export WEBSOCKETDIR

start() {
	OPTIONS="${OPTIONS} -P ${PIDFILE}"
	OPTIONS="${OPTIONS} -d"

	sleep 1
	route add -net 224.0.0.0 netmask 240.0.0.0 $WIFI_DEVICE

	if [ -e /var/run/gpiod.pid ] && [ -z "$MDEV" ]; then
		exit 0
	fi
	printf 'Starting %s: ' "$DAEMON"
	start-stop-daemon -S -q -x "/usr/bin/$DAEMON" \
		-- $OPTIONS
	status=$?
	if [ "$status" -eq 0 ]; then
		echo "OK"
	else
		echo "FAIL"
	fi
	return "$status"
}

stop() {
	printf 'Stopping %s: ' "$DAEMON"
	start-stop-daemon -K -q -p "$PIDFILE"
	status=$?
	if [ "$status" -eq 0 ]; then
		rm -f "$PIDFILE"
		echo "OK"
	else
		echo "FAIL"
	fi
	return "$status"
}

restart() {
	stop
	sleep 1
	start
}

case "$1" in
	start|stop|restart)
		"$1";;
	reload)
		# Restart, since there is no true "reload" feature.
		restart;;
	*)
		echo "Usage: $0 {start|stop|restart|reload}"
		exit 1
esac
