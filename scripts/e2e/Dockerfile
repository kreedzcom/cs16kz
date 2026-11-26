FROM ubuntu:24.04

RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl unzip \
        libc6:i386 libstdc++6:i386 libgcc-s1:i386 zlib1g:i386 libssl3t64:i386 libsqlite3-0:i386 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 hlds \
    && install -d -o hlds -g hlds /opt/hlds /opt/steamcmd

USER hlds
WORKDIR /opt/steamcmd
RUN curl -fsSL --retry 3 https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz | tar -xz \
    && for attempt in 1 2 3 4 5; do \
        timeout 5m ./steamcmd.sh +force_install_dir /opt/hlds +login anonymous \
            +app_set_config 90 mod cstrike +app_update 90 -beta steam_legacy validate +quit \
        && test -s /opt/hlds/cstrike/maps/de_dust2.bsp \
        && test -s /opt/hlds/cstrike/dlls/cs.so && break; \
        sleep 5; \
    done \
    && test -x /opt/hlds/hlds_linux \
    && test -s /opt/hlds/cstrike/maps/de_dust2.bsp \
    && test -s /opt/hlds/cstrike/dlls/cs.so \
    && mkdir -p ~/.steam/sdk32 \
    && ln -sf /opt/steamcmd/linux32/steamclient.so ~/.steam/sdk32/steamclient.so

WORKDIR /opt/hlds
RUN curl -fsSL --retry 3 -o /tmp/rehlds.zip \
        https://github.com/rehlds/ReHLDS/releases/download/3.14.0.857/rehlds-bin-3.14.0.857.zip \
    && curl -fsSL --retry 3 -o /tmp/metamod.zip \
        https://github.com/rehlds/Metamod-R/releases/download/1.3.0.149/metamod-bin-1.3.0.149.zip \
    && curl -fsSL --retry 3 -o /tmp/amxx.tar.gz \
        https://github.com/alliedmodders/amxmodx/releases/download/1.10.0.5488/amxmodx-1.10.0-git5488-base-linux.tar.gz \
    && echo 'b537f5c5972294afcd5b022e5ffc44627738dfb029ad1b2eab5dc482c8812391  /tmp/amxx.tar.gz' | sha256sum -c \
    && unzip -q /tmp/rehlds.zip -d /tmp/rehlds \
    && unzip -q /tmp/metamod.zip -d /tmp/metamod \
    && cp /tmp/rehlds/bin/linux32/*.so . \
    && mkdir -p cstrike/addons/metamod/dlls \
    && find /tmp/metamod -name metamod_i386.so -exec cp {} cstrike/addons/metamod/dlls/ \; \
    && test -s cstrike/addons/metamod/dlls/metamod_i386.so \
    && tar -xzf /tmp/amxx.tar.gz -C cstrike \
    && rm -r /tmp/rehlds /tmp/metamod \
    && rm /tmp/rehlds.zip /tmp/metamod.zip /tmp/amxx.tar.gz

COPY start-game.sh /usr/local/bin/kz-e2e-start
ENTRYPOINT ["/bin/bash", "/usr/local/bin/kz-e2e-start"]
