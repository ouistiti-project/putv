#!/bin/sh
#
# Starts putv.
#

SYSCONFDIR=/etc/
RUNDIR=/var/run
SBINDIR=/usr/sbin
BINDIR=/usr/bin
LIBDIR=/usr/lib

OPTIONS=""
. /etc/setting.d/putv.in
[ -r "/etc/default/$DAEMON" ] && . "/etc/default/$DAEMON"

client() {
  chmod a+rwx ${WEBSOCKETDIR}
  ${CDISPLAY} -D ${OPTIONS_CLIENTS}
  if [ -c ${CINPUT_DEVICE} ]; then
    ${CINPUT} -D ${OPTIONS_CLIENTS} ${OPTIONS_CINPUT}
  fi
}

start() {
  OPTIONS="${OPTIONS} -m ${MEDIA} -a -l -r"
  OPTIONS="${OPTIONS} -R ${WEBSOCKETDIR}"
  #OPTIONS="${OPTIONS} -u ${USER}"
  OPTIONS="${OPTIONS} -L ${LOGFILE}"
  OPTIONS="${OPTIONS} -p ${RUNDIR}/${DAEMON}.pid"
  if [ "${OUTPUT}" != "" ]; then
    OPTIONS="${OPTIONS} -o ${OUTPUT}"
  fi

  ${DAEMON} ${OPTIONS} -D
  if [ ! $? -eq 0 ]; then
    exit 1
  fi
}
stop() {
  OPTIONS="${OPTIONS} -p ${RUNDIR}/${DAEMON}.pid"

  ${DAEMON} ${OPTIONS} -K
}
restart() {
	stop
	start
}

case "$1" in
  client)
	client;
	;;
  rising)
	start;
	;;
  start)
	start
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

