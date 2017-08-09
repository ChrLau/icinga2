DAEMON=@CMAKE_INSTALL_FULL_SBINDIR@/icinga2
ICINGA2_CONFIG_FILE=@CMAKE_INSTALL_FULL_SYSCONFDIR@/icinga2/icinga2.conf
ICINGA2_RUN_DIR=@ICINGA2_RUNDIR@
ICINGA2_STATE_DIR=@CMAKE_INSTALL_FULL_LOCALSTATEDIR@
ICINGA2_PID_FILE=$ICINGA2_RUN_DIR/icinga2/icinga2.pid
ICINGA2_ERROR_LOG=@CMAKE_INSTALL_FULL_LOCALSTATEDIR@/log/icinga2/error.log
ICINGA2_STARTUP_LOG=@CMAKE_INSTALL_FULL_LOCALSTATEDIR@/log/icinga2/startup.log
ICINGA2_LOG=@CMAKE_INSTALL_FULL_LOCALSTATEDIR@/log/icinga2/icinga2.log
ICINGA2_CACHE_DIR=$ICINGA2_STATE_DIR/cache/icinga2
ICINGA2_USER=@ICINGA2_USER@
ICINGA2_GROUP=@ICINGA2_GROUP@
ICINGA2_COMMAND_GROUP=@ICINGA2_COMMAND_GROUP@
