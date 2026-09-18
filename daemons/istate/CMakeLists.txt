# istate daemon (REQ §7)
set(DAEMON_SRC
  src/config_service.cpp
  src/core.cpp
  src/hubs.cpp
  src/northbound.cpp
  src/orchestrator.cpp
  src/provisioning.cpp
  src/pst.cpp
  src/table.cpp)
add_library(instr_istate_lib STATIC ${DAEMON_SRC})
target_include_directories(instr_istate_lib PUBLIC
  include
  ${CMAKE_SOURCE_DIR}/daemons/alarms/include
  ${CMAKE_SOURCE_DIR}/daemons/backup/include
  ${CMAKE_SOURCE_DIR}/daemons/faults/include
  ${CMAKE_SOURCE_DIR}/daemons/hardware/include
  ${CMAKE_SOURCE_DIR}/daemons/logmanager/include
  ${CMAKE_SOURCE_DIR}/daemons/monitord/include
  ${CMAKE_SOURCE_DIR}/daemons/networkd/include
  ${CMAKE_SOURCE_DIR}/daemons/ota/include
  ${CMAKE_SOURCE_DIR}/daemons/telemetry/include)
# instr_alarm_ids carries the generated alarm-id header (§8): istate declares
# its own alarms by id, from the same catalog every other component uses.
target_link_libraries(instr_istate_lib PUBLIC
  instr_common instr_platform instr_transport instr_protos instr_ctrl instr_alarm_ids
  instr_networkd_lib
  instr_monitord_lib
  instr_hardware_lib
  instr_faults_lib
  instr_alarms_lib
  instr_logmanager_lib
  instr_telemetry_lib
  instr_ota_lib
  instr_backup_lib)
add_dependencies(instr_istate_lib alarm_gen_header)

add_executable(istate src/main.cpp)
target_link_libraries(istate PRIVATE
  instr_istate_lib
  instr_protos
  instr_networkd_lib
  instr_monitord_lib
  instr_hardware_lib
  instr_faults_lib
  instr_alarms_lib
  instr_logmanager_lib
  instr_telemetry_lib
  instr_ota_lib
  instr_backup_lib)
