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
  printf "Starting ouiradio: "

  MEDIA_DIR=$(echo ${MEDIA} | grep "file:\/\/" | sed 's,^file\:\/\/,,g')
  if [ -n ${MEDIA_DIR} ] && [ ! -d ${MEDIA_DIR} ]; then
    mkdir -p ${MEDIA_DIR}
    mount ${MEDIA_DIR}
  fi
  OPTIONS="${OPTIONS} -m ${MEDIA}"
  OPTIONS="${OPTIONS} -a -l -r"
#  OPTIONS="${OPTIONS} -a -r"
  OPTIONS="${OPTIONS} -R ${WEBSOCKETDIR}"
#  OPTIONS="${OPTIONS} -u ${USER}"
  OPTIONS="${OPTIONS} -L ${LOGFILE}"
  OPTIONS="${OPTIONS} -p ${RUNDIR}/${DAEMON}.pid"
  if [ "${OUTPUT}" != "" ]; then
    OPTIONS="${OPTIONS} -o ${OUTPUT}"
  fi

  ${DAEMON} ${OPTIONS} -D
  if [ ! $? -eq 0 ]; then
    echo "KO"
    exit 1
  fi
  echo "OK"
}
stop() {
  OPTIONS="${OPTIONS} -p ${RUNDIR}/${DAEMON}.pid"

  printf "Stopping ouiradio: "
  ${DAEMON} ${OPTIONS} -K
  if [ -e ${RUNDIR}/${DAEMON}.pid ]; then
	rm ${RUNDIR}/${DAEMON}.pid
  fi
  echo "OK"
}
restart() {
	client
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
    client
    # check gpiod to start or not
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

