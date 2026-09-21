# tools — one _ctrl per daemon with full operation coverage (REQ §3.2).
# The command table lives in the daemon library (single source of truth);
# each tool links its daemon's library so help and completion are structural.
set(TOOLS
  istate_ctrl:istate
  network_ctrl:networkd
  monitor_ctrl:monitord
  hardware_ctrl:hardware
  fault_ctrl:faults
  alarm_ctrl:alarms
  log_ctrl:logmanager
  telemetry_ctrl:telemetry
  ota_ctrl:ota
  backup_ctrl:backup
)
foreach(pair IN LISTS TOOLS)
  string(REPLACE ":" ";" pair_list ${pair})
  list(GET pair_list 0 prog)
  list(GET pair_list 1 daemon)
  add_executable(${prog} ${prog}/main.cpp)
  target_link_libraries(${prog} PRIVATE instr_${daemon}_lib instr_ctrl)
  target_compile_definitions(${prog} PRIVATE
    INSTR_CTRL_PROG="${prog}"
    INSTR_CTRL_SOCKET="/run/instrumentation/${daemon}-ctrl.sock")
endforeach()

add_executable(instr-istate-watchdog istate_watchdog.cpp)
target_link_libraries(instr-istate-watchdog PRIVATE instr_common)

# ---- standalone PREEMPT_RT demonstration app (task 005) ----
# Not a daemon: a one-shot diagnostic run by an operator on a target or a
# developer on a host. Core/main split so the statistics, identity parsing
# and report serialisation are unit-testable without real timing (shared CI
# runners must never gate on measured latency values). Links only
# instr_common, like the watchdog: no proto, no ctrl, no istate.
add_library(instr_rttest_lib STATIC rt-test/rttest.cpp)
target_include_directories(instr_rttest_lib PUBLIC rt-test)
target_link_libraries(instr_rttest_lib PUBLIC instr_common)

add_executable(instr-rt-test rt-test/main.cpp)
target_link_libraries(instr-rt-test PRIVATE instr_rttest_lib)
