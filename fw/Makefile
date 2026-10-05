.PHONY: app bl clean size_report size_report_files

all: bl app ota

bl:
	@$(MAKE) -C bootloader/

app:
	@$(MAKE) -C application/ && { \
		echo "=== TEMP-DEBUG: map sections ==="; \
		grep -E '^\.[a-zA-Z_]+ +0x' _build/pixljs.map | head -60; \
		echo "=== TEMP-DEBUG: memory config ==="; \
		grep -A8 "Memory Configuration" _build/pixljs.map; \
	} || { \
		echo "=== TEMP-DEBUG: link failed, dumping per-object RAM usage ==="; \
		arm-none-eabi-size _build/pixljs/*.o | sort -k4 -n -r | head -40; \
		echo "=== TEMP-DEBUG: objects sorted by data ==="; \
		arm-none-eabi-size _build/pixljs/*.o | sort -k3 -n -r | head -20; \
		echo "=== TEMP-DEBUG: map file (if written) ==="; \
		sed -n '1,80p' _build/pixljs.map 2>/dev/null; \
		exit 1; }

ota:
	@$(MAKE) -C application/ full ota

clean:
	@rm -rf _build/

size_report:
	@$(MAKE) -C application/ size_report

size_report_files:
	@$(MAKE) -C application/ size_report_files
