# main project for qemu-aarch64
MODULES += \
	app/shell

MODULES += lib/debuglog
WITH_DEBUG_LINEBUFFER=true

include project/virtual/test.mk
include project/virtual/fs.mk
include project/virtual/minip.mk
include project/target/qemu-virt-a53.mk

