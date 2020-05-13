#!/bin/sh
#
# Starts putv.
#

SYSCONFDIR=/etc/
RUNDIR=/var/run
SBINDIR=/usr/sbin
BINDIR=/usr/bin
LIBDIR=/usr/lib

. /etc/setting.d/putv.in
. /etc/setting.d/wifiap.in

export WEBSOCKETDIR

DAEMON=upnprender
OPTIONS="-o putv"
OPTIONS="${OPTIONS} -I 192.168.10.254"

start() {
	sleep 2
	route add -net 224.0.0.0 netmask 240.0.0.0 $WIFI_DEVICE
	printf "Starting UPnP renderer: "
	echo ${BINDIR}/${DAEMON} -d -P ${RUNDIR}/${DAEMON}.pid ${OPTIONS}
	${BINDIR}/${DAEMON} -d -P ${RUNDIR}/${DAEMON}.pid ${OPTIONS}
	if [ $? -eq 0 ]; then
		echo "OK"
	else
		echo "KO"
	fi
}
stop() {
	printf "Stopping UPnP renderer: "
	if [ -e ${RUNDIR}/${DAEMON}.pid ]; then
		start-stop-daemon -K -q -p ${RUNDIR}/${DAEMON}.pid
	fi
	echo "OK"
}
restart() {
	stop
	start
}

case "$1" in
  rising)
    start
    ;;
  start)
    if [ ! -e ${RUNDIR}/gpiod.pid ]; then
      start
    fi
    ;;
  falling)
    stop
    ;;
  stop)
    stop
    ;;
  restart|reload)
    restart
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac

exit $?

