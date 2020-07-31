#!/bin/sh
#
# Starts putv.
#

DAEMON="putv"
PIDFILE="/var/run/$DAEMON.pid"
MEDIA="file:///media"
WEBSOCKETDIR="/var/run/websocket"
USER="www-data"
LOGFILE="/var/log/$DAEMON.log"
FILTER="pcm_stereo"
OUTPUT=""

OPTIONS=""
[ -r "/etc/default/$DAEMON" ] && . "/etc/default/$DAEMON"

client() {
  chmod a+rwx ${WEBSOCKETDIR}
  ${CDISPLAY} -D ${OPTIONS_CLIENTS}
  if [ -c ${CINPUT_DEVICE} ]; then
    ${CINPUT} -D ${OPTIONS_CLIENTS} ${OPTIONS_CINPUT}
  fi
}

start() {
	OPTIONS="${OPTIONS} -m ${MEDIA}"
	OPTIONS="${OPTIONS} -a -l -r"
# 	OPTIONS="${OPTIONS} -a -r"
	OPTIONS="${OPTIONS} -R ${WEBSOCKETDIR}"
#	OPTIONS="${OPTIONS} -u ${USER}"
	OPTIONS="${OPTIONS} -L ${LOGFILE}"
	OPTIONS="${OPTIONS} -p ${PIDFILE}"
	OPTIONS="${OPTIONS} -f ${FILTER}"
	if [ "${PRIORITY}" != "" ]; then
		OPTIONS="${OPTIONS} -P ${PRIORITY}"
	fi
	if [ "${OUTPUT}" != "" ]; then
		OPTIONS="${OPTIONS} -o ${OUTPUT}"
	fi
	OPTIONS="${OPTIONS} -D"

	if [ -e /var/run/gpiod.pid ] && [ -z "$GPIO" ]; then
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
