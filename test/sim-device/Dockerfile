# ============================================================================
#  Acadia Instrumentation — simulated device (REQ §12)
#
#  "Simulated device — the full daemon set in a container, so HQ and
#   Configuration can be developed without hardware."
#
#  The image runs the real binaries: istate plus all nine spokes, over real
#  gRPC on real Unix domain sockets, against the simulated platform layer.
#  Nothing is mocked except the hardware itself, so a Configuration or HQ
#  developer talks to the same interfaces a device exposes.
#
#  Build:  docker build -f test/sim-device/Dockerfile -t acadia/sim-device .
#  Run:    docker run --rm -it -v acadia-pst:/pst acadia/sim-device
#  Talk to it:
#          docker exec -it <id> istate_ctrl status
#          docker exec -it <id> monitor_ctrl sample-once
#  The Configuration UI connects to /run/instrumentation/configuration.sock;
#  mount that directory to reach it from the host.
# ============================================================================

# ---- build stage ----
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config python3 \
        protobuf-compiler protobuf-compiler-grpc \
        libprotobuf-dev libgrpc++-dev libyaml-cpp-dev libgtest-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . /src
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
 && cmake --build /build -j"$(nproc)" \
 && mkdir -p /out \
 && cp /build/daemons/*/istate /build/daemons/*/networkd /build/daemons/*/monitord \
       /build/daemons/*/hardware /build/daemons/*/faults /build/daemons/*/alarms \
       /build/daemons/*/logmanager /build/daemons/*/telemetry /build/daemons/*/ota \
       /build/daemons/*/backup /build/tools/*_ctrl /out/

# ---- runtime stage ----
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libprotobuf32 libgrpc++1.51 libyaml-cpp0.7 libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Binaries build under /build/daemons/<d>/ and /build/tools/; the build
# stage collects them into /out.
COPY --from=build /out/istate      /usr/bin/instr-istate
COPY --from=build /out/networkd    /usr/bin/instr-networkd
COPY --from=build /out/monitord    /usr/bin/instr-monitord
COPY --from=build /out/hardware    /usr/bin/instr-hardware
COPY --from=build /out/faults      /usr/bin/instr-faults
COPY --from=build /out/alarms      /usr/bin/instr-alarms
COPY --from=build /out/logmanager  /usr/bin/instr-logmanager
COPY --from=build /out/telemetry   /usr/bin/instr-telemetry
COPY --from=build /out/ota         /usr/bin/instr-ota
COPY --from=build /out/backup      /usr/bin/instr-backup
COPY --from=build /out/istate_ctrl    /usr/bin/istate_ctrl
COPY --from=build /out/network_ctrl   /usr/bin/network_ctrl
COPY --from=build /out/monitor_ctrl   /usr/bin/monitor_ctrl
COPY --from=build /out/hardware_ctrl  /usr/bin/hardware_ctrl
COPY --from=build /out/fault_ctrl     /usr/bin/fault_ctrl
COPY --from=build /out/alarm_ctrl     /usr/bin/alarm_ctrl
COPY --from=build /out/log_ctrl       /usr/bin/log_ctrl
COPY --from=build /out/telemetry_ctrl /usr/bin/telemetry_ctrl
COPY --from=build /out/ota_ctrl       /usr/bin/ota_ctrl
COPY --from=build /out/backup_ctrl    /usr/bin/backup_ctrl

# Image data: schemas, catalogs, descriptors and the default YAML set that
# seeds /pst on first boot — the same files the Yocto recipe installs.
COPY schemas/                     /usr/share/instrumentation/schemas/
COPY catalog/alarms.yaml          /usr/share/instrumentation/catalog/alarms.yaml
COPY catalog/fault-rules.yaml     /etc/instrumentation/fault-rules.yaml
COPY system/defaults/             /usr/share/instrumentation/defaults/
# The simulated device presents itself as the Acadia rpi-5 board (the x86-64
# descriptors were pruned with the other non-Acadia boards in T-P32; the
# simulated platform layer stands in for the hardware either way).
COPY descriptors/devices-rpi-5.yaml          /etc/instrumentation/devices.yaml
COPY descriptors/capabilities-rpi-5.yaml     /etc/instrumentation/capabilities.yaml
COPY descriptors/resource-profile-rpi-5.yaml /etc/instrumentation/resource-profile.yaml

COPY test/sim-device/entrypoint.sh /usr/bin/sim-device
RUN chmod +x /usr/bin/sim-device

# There is no systemd here, so istate's orchestration requests are recorded
# rather than executed and this script starts the spokes instead. Everything
# else — registration, config delivery, streaming, liveness — is the real
# code path.
ENV INSTR_PLATFORM=simulated
VOLUME ["/pst"]
ENTRYPOINT ["/usr/bin/sim-device"]
