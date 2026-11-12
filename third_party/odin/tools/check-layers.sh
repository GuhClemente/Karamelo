#!/bin/sh
# Includes must go common <- machine <- host: a layer includes only the ones before it
INCLUDE='^[[:space:]]*#[[:space:]]*include[[:space:]]*'
bad=$(
	grep -rnE --include='*.cpp' --include='*.hpp' --include='*.h' "$INCLUDE\"odin/host/" src/odin/machine | grep -vE \
		-e '^src/odin/machine/boards/hikaru/hikaru\.hpp:[0-9]+:#include "odin/host/app/games\.hpp"$' \
		-e '^src/odin/machine/boards/hikaru/hikaru\.hpp:[0-9]+:#include "odin/host/app/user_files\.hpp"$' \
		-e '^src/odin/machine/chips/card_reader\.cpp:[0-9]+:#include "odin/host/app/user_files\.hpp"$' \
		-e '^src/odin/machine/cabinet/netbd_ring\.cpp:[0-9]+:#include "odin/host/input/sockets\.hpp"$'
	grep -rnE --include='*.cpp' --include='*.hpp' --include='*.h' "$INCLUDE\"odin/(machine|host)/" src/odin/common
	grep -rnE --include='*.cpp' --include='*.hpp' --include='*.h' "$INCLUDE[<\"](SDL|GL/|vulkan|jansson|imgui)" \
		src/odin/machine src/odin/common
	grep -rnE --include='*.cpp' --include='*.hpp' --include='*.h' \
		"$INCLUDE[<\"](windows\.h|winsock|ws2tcpip|sys/|unistd|netdb|netinet|fcntl|dlfcn)" \
		src/odin/machine src/odin/common | grep -vE \
		-e '^src/odin/machine/core/host_code\.cpp:[0-9]+:#include <(windows|sys/mman)\.h>$' \
		-e '^src/odin/machine/cabinet/netbd_ring\.cpp:[0-9]+:#include <windows\.h>$'
)
if [ -n "$bad" ]; then
	echo "$bad"
	echo "check-layers: includes across the layers"
	exit 1
fi
echo "check-layers: common <- machine <- host, only the known exceptions"
