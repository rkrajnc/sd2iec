# architecture-dependent additional targets and manual dependencies

# A little helper target for the maintainer =)
copy:
	cp $(TARGET).bin /mbed

ifneq ($(CONFIG_BOOTLOADER),y)
bin:
	$(E) "  CHKSUM $(TARGET).bin"
	$(Q)scripts/lpc17xx/lpc_checksum.pl $(TARGET).bin
endif
