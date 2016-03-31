# Absolute path to kubos-core directory
KUBOS_CORE ?= $(CURDIR)
KUBOS_CORE := $(abspath $(KUBOS_CORE))

# Absolute path to Kubos modules directory
KUBOS_MODULES ?= $(KUBOS_CORE)/modules
KUBOS_MODULES := $(abspath $(KUBOS_MODULES))

KUBOS_MODULE_DIRS = $(foreach module,$(USEMODULE),$(wildcard $(KUBOS_MODULES)/$(module)))
EXTERNAL_MODULE_DIRS += $(KUBOS_MODULE_DIRS)

CFLAGS += $(foreach dir,$(KUBOS_MODULE_DIRS),-I$(dir))
INCLUDES += $(foreach dir,$(KUBOS_MODULE_DIRS),-I$(dir))
LINKFLAGS += -lm

include $(KUBOS_CORE)/build/deps.mk
-include $(KUBOS_CORE)/build/$(BOARD).mk
