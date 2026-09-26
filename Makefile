# This file is based off DevkitARM's Makefile template and the included DevkitARM make files.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# the prefix on the compiler executables
#---------------------------------------------------------------------------------
PREFIX          :=      arm-none-eabi-

export CC       :=      $(PREFIX)gcc
export CXX      :=      $(PREFIX)g++
export AS       :=      $(PREFIX)as
export AR       :=      $(PREFIX)gcc-ar
export OBJCOPY  :=      $(PREFIX)objcopy
export STRIP    :=      $(PREFIX)strip
export NM       :=      $(PREFIX)gcc-nm
export RANLIB   :=      $(PREFIX)gcc-ranlib

#---------------------------------------------------------------------------------
%.o: %.c
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@ $(ERROR_FILTER)

#---------------------------------------------------------------------------------
%.o: %.m
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(OBJCFLAGS) -c $< -o $@ $(ERROR_FILTER)

#---------------------------------------------------------------------------------
%.o: %.s
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) -c $< -o $@ $(ERROR_FILTER)

#---------------------------------------------------------------------------------
%.o: %.S
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) -c $< -o $@ $(ERROR_FILTER)

#---------------------------------------------------------------------------------
%.elf:
	echo linking $(notdir $@)
	$(LD)  $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------

#             <-- Change to EU if required
REGION := EU
ROM := rom.nds
ROM_OUT := out.nds

TARGET		:=	out
BUILD		:=	build
SOURCES		:=	src src/cot src/uplink
INCLUDES	:=	include pmdsky-debug/headers src/uplink src/uplink/tinyusb/src src
OPT_LEVEL := -O2

# TinyUSB (vendored under src/uplink/tinyusb, pinned to
# 8eeddaab364e413153ebd0a8302f85dfb2e60e9f, the gitlink of
# LNH-team/dspico-usb-examples). Only the device/CDC/none-OS subset.
TUSB_DIRS	:=	src/uplink/tinyusb/src src/uplink/tinyusb/src/common \
		src/uplink/tinyusb/src/device src/uplink/tinyusb/src/class/cdc
TUSB_CFILES	:=	tusb.c tusb_fifo.c usbd.c usbd_control.c cdc_device.c

# The uplink objects are compiled without LTO so they remain real objects
# with their original file names. LTO bytecode objects are re-emitted at
# final link as ltrans*.ltrans.o, which defeats the per-file linker-script
# wildcards (uplink.o, tusb.o, ...) that place the uplink code in the
# 0x23A8000 headroom region separate from the mod at 0x23D7FF0.
# NOTE: keep this block OUTSIDE the ifneq below so the target-specific rule
# also exists in the inner (build/) make invocation that compiles objects.
UPLINK_OBJS        :=      uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o \
                    dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o
$(UPLINK_OBJS): CFLAGS += -fno-lto

# Change to "RELEASE_CONFIG := -DNDEBUG" for release builds without asserts and logs
RELEASE_CONFIG := -DDEBUG

PYTHON := python

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-marm -mno-thumb-interwork

CFLAGS	:=	-g -Wall $(OPT_LEVEL) $(RELEASE_CONFIG) $(SP_EFFECT_COMPAT) \
 			-march=armv5te -mtune=arm946e-s -fomit-frame-pointer -fno-short-enums \
			-ffast-math -fno-builtin \
			-fmacro-prefix-map=$(realpath $(CURDIR)/..)=. \
			$(ARCH)

# No -flto: LTO bytecode objects are re-emitted at final link as temp
# ltrans*.ltrans.o files, which breaks the per-object linker-script
# wildcards that place the uplink code at 0x23A8000 separately from the
# mod at 0x23D7FF0 (the merged ltrans partition ends up in the first
# .text pattern). Plain -O2 objects keep their file names throughout.
CFLAGS	+=	$(INCLUDE) -DARM9

# Those are to be set by command line arguments.
CFLAGS  +=  $(EXTRA_CFLAGS)

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-T $(CURDIR)/../symbols/generated_$(REGION).ld \
			-T $(CURDIR)/../symbols/custom_$(REGION).ld -T $(CURDIR)/../linker.ld \
			-g $(ARCH) -Wl,-Map,$(notdir $*.map) -Xlinker -no-enum-size-warning -nostdlib

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
# ARM9 has no hardware divide; -nostdlib suppresses the implicit -lgcc, so
# pull in the __aeabi_* libgcc helpers (e.g. __aeabi_uidivmod) explicitly.
# LIBS expands after $(OFILES) in the link rule, so the archive is scanned
# after the objects that reference its members.
LIBS	:=	-lgcc
 
 
#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	
 
#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
 
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(TUSB_DIRS),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) \
			$(TUSB_CFILES)
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.bin)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(BINFILES:.bin=.o) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
 
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)
 
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
 
#---------------------------------------------------------------------------------
.PHONY: $(BUILD)
$(BUILD): symbols/generated_$(REGION).ld
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

.PHONY: buildobjs
buildobjs:
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile buildobjs
 
#---------------------------------------------------------------------------------
.PHONY: clean
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).bin $(TARGET).asm $(ROM_OUT).nds symbols/generated_*.ld
 
#---------------------------------------------------------------------------------
else
 
DEPENDS	:=	$(OFILES:.o=.d)
 
#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------

$(OUTPUT).bin : $(OUTPUT).elf
	arm-none-eabi-objcopy -O binary $(OUTPUT).elf $(OUTPUT).bin

$(OUTPUT).elf	:	$(OFILES)

.PHONY: buildobjs
buildobjs: $(OFILES)

-include $(DEPENDS)
 
#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------

symbols/generated_$(REGION).ld:
	$(PYTHON) scripts/generate_linkerscript.py $(REGION)

.PHONY: patch
patch: build
	$(PYTHON) scripts/patch.py $(REGION) $(ROM) $(OUTPUT).bin $(OUTPUT).elf $(ROM_OUT)

.PHONY: asmdump
asmdump: build
	arm-none-eabi-objdump -S -d $(OUTPUT).elf > $(OUTPUT).asm

.PHONY: header-comments
header-comments:
	cd pmdsky-debug/headers && $(PYTHON) add_header_docstrings.py
