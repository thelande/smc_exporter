package main

import (
	"net/http"
	"os"
	"os/user"
	"runtime"

	"github.com/go-kit/log"
	"github.com/go-kit/log/level"
	"golang.org/x/exp/slices"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"
	"github.com/prometheus/exporter-toolkit/web"
	"github.com/prometheus/exporter-toolkit/web/kingpinflag"

	"github.com/prometheus/common/version"
	kingpin "gopkg.in/alecthomas/kingpin.v2"

	"github.com/thelande/smc_exporter/collector"
	"github.com/thelande/smc_exporter/smc"
)

var LOG_LEVELS = []string{"debug", "info", "warn", "error"}

func main() {
	var (
		logLevel = kingpin.Flag(
			"logging.level",
			"Log level",
		).Default("info").String()
		metricsPath = kingpin.Flag(
			"web.telemetry-path",
			"Path under which to expose metrics.",
		).Default("/metrics").String()
		maxProcs = kingpin.Flag(
			"runtime.gomaxprocs", "The target number of CPUs Go will run on (GOMAXPROCS)",
		).Envar("GOMAXPROCS").Default("1").Int()
		toolkitFlags     = kingpinflag.AddFlags(kingpin.CommandLine, ":9190")
		sensorLabelsFile = kingpin.Flag(
			"config.labelsfile-path",
			"Path to file containing sensor labels",
		).Default("sensors.json").String()
	)

	kingpin.Version(version.Print("smc_exporter"))
	kingpin.CommandLine.UsageWriter(os.Stdout)
	kingpin.HelpFlag.Short('h')
	kingpin.Parse()

	logger := log.NewLogfmtLogger(log.NewSyncWriter(os.Stderr))
	if !slices.Contains(LOG_LEVELS, *logLevel) {
		level.Error(logger).Log("msg", "Invalid log level", "logLevel", *logLevel)
		os.Exit(1)
	}
	logger = level.NewFilter(logger, level.Allow(level.ParseDefault(*logLevel, level.InfoValue())))

	level.Info(logger).Log("msg", "Starting smc_exporter", "version", version.Info())
	level.Info(logger).Log("msg", "Build context", "build_context", version.BuildContext())
	if user, err := user.Current(); err == nil && user.Uid == "0" {
		level.Warn(logger).Log("msg", "SMC Exporter is running as root user. This exporter is designed to run as unprivileged user, root is not required.")
	}
	runtime.GOMAXPROCS(*maxProcs)
	level.Debug(logger).Log("msg", "Go MAXPROCS", "procs", runtime.GOMAXPROCS(0))

	reg := prometheus.NewPedanticRegistry()

	// Add the standard process and Go metrics to the custom registry.
	reg.MustRegister(
		collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}),
		collectors.NewGoCollector(),
	)

	sensorLabels := smc.GetAllSensorLabels(*sensorLabelsFile)
	level.Info(logger).Log("msg", "Loaded sensor labels", "count", len(sensorLabels))

	reg.MustRegister(collector.NewSmcCollector(logger, sensorLabels))

	http.Handle(*metricsPath, promhttp.HandlerFor(reg, promhttp.HandlerOpts{}))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`<html>
			<head><title>SMC Exporter</title></head>
			<body>
			<h1>SMC Exporter</h1>
			<p><a href="` + *metricsPath + `">Metrics</a></p>
			</body>
			</html>`))
	})

	server := http.Server{}
	if err := web.ListenAndServe(&server, toolkitFlags, logger); err != nil {
		level.Error(logger).Log("err", err)
		os.Exit(1)
	}
}
