PREFIX=/usr/local

all: smc_exporter

smc_exporter: go.mod go.sum smc_exporter.go smc/smc.go collector/collector.go libsmc/libsmc.a
	go build .

libsmc/libsmc.a:
	$(MAKE) -C libsmc

install: smc_exporter
	install -m755 smc_exporter $(PREFIX)/bin/smc_exporter
	install -m755 -d $(PREFIX)/etc/smc_exporter
	install -m644 sensors.json $(PREFIX)/etc/smc_exporter/sensors.json
	install -m644 smc_exporter.plist ~/Library/LaunchAgents/smc_exporter.plist
	if launchctl list | grep -q smc_exporter; then launchctl unload smc_exporter.plist; fi
	launchctl load smc_exporter.plist

uninstall:
	launchctl stop smc_exporter.plist
	launchctl unload smc_exporter.plist
	rm -rf $(PREFIX)/bin/smc_exporter $(PREFIX)/etc/smc_exporter ~/Library/LaunchAgents/smc_exporter.plist

clean:
	rm -f smc_exporter libsmc/libsmc.a

.PHONY: clean install
