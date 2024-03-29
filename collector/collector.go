package collector

import (
	"bytes"
	"strings"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/thelande/smc_exporter/smc"
	"golang.org/x/exp/constraints"
	"golang.org/x/exp/slices"
	"golang.org/x/sys/unix"

	"github.com/go-kit/log"
	"github.com/go-kit/log/level"
)

const namespace = "smc"

var labels = []string{"sensor", "label"}

type Numeric interface {
	constraints.Float | constraints.Integer
}

// SmcCollector implements the prometheus.Collector interface
type SmcCollector struct {
	logger                    log.Logger
	tempMetric                *prometheus.Desc
	powerMetric               *prometheus.Desc
	voltageMetric             *prometheus.Desc
	currentMetric             *prometheus.Desc
	fanMetric                 *prometheus.Desc
	batteryChargeMetric       *prometheus.Desc
	batteryChargePctMetric    *prometheus.Desc
	batteryCyclesMetric       *prometheus.Desc
	batteryChargeRemainMetric *prometheus.Desc
	info                      *prometheus.Desc
	sensorLabels              map[string][]string
}

func NewSmcCollector(logger log.Logger, sensorLabels map[string][]string) *SmcCollector {
	return &SmcCollector{
		logger:       logger,
		sensorLabels: sensorLabels,
		tempMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "temp_celsius"),
			"Apple System Management Control (SMC) monitor for temperature",
			labels,
			nil,
		),
		powerMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "power_watts"),
			"Apple System Management Control (SMC) monitor for power",
			labels,
			nil,
		),
		voltageMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "voltage_volts"),
			"Apple System Management Control (SMC) monitor for voltage",
			labels,
			nil,
		),
		currentMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "current_amps"),
			"Apple System Management Control (SMC) monitor for current",
			labels,
			nil,
		),
		fanMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "fan_rpms"),
			"Apple System Management Control (SMC) monitor for fans",
			labels,
			nil,
		),
		batteryChargeMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "battery_charge_mha"),
			"Apple System Management Control (SMC) monitor for the battery",
			labels,
			nil,
		),
		batteryChargePctMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "battery_charge_percent"),
			"Apple System Management Control (SMC) monitor for the battery",
			labels,
			nil,
		),
		batteryCyclesMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "battery_cycles"),
			"Apple System Management Control (SMC) monitor for the battery",
			labels,
			nil,
		),
		batteryChargeRemainMetric: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "", "battery_charge_secs"),
			"Apple System Management Control (SMC) monitor for the battery",
			labels,
			nil,
		),
		info: prometheus.NewDesc(
			prometheus.BuildFQName(namespace, "uname", "info"),
			"Labeled system information as provided by the uname system call.",
			[]string{
				"sysname",
				"release",
				"version",
				"machine",
				"nodename",
			},
			nil,
		),
	}
}

// Describe implements the prometheus.Collector interface
func (s SmcCollector) Describe(ch chan<- *prometheus.Desc) {
	ch <- s.tempMetric
	ch <- s.powerMetric
	ch <- s.voltageMetric
	ch <- s.currentMetric
	ch <- s.fanMetric
	ch <- s.batteryChargeMetric
	ch <- s.batteryChargePctMetric
	ch <- s.batteryCyclesMetric
	ch <- s.batteryChargeRemainMetric
	ch <- s.info
}

func (s *SmcCollector) getInfoMetric() (*prometheus.Metric, error) {
	uname := unix.Utsname{}
	if err := unix.Uname(&uname); err != nil {
		level.Error(s.logger).Log("Failed to get uname", "err", err)
		return nil, err
	}

	metric := prometheus.MustNewConstMetric(
		s.info, prometheus.GaugeValue, 1,
		string(uname.Sysname[:bytes.IndexByte(uname.Sysname[:], 0)]),
		string(uname.Release[:bytes.IndexByte(uname.Release[:], 0)]),
		string(uname.Version[:bytes.IndexByte(uname.Version[:], 0)]),
		string(uname.Machine[:bytes.IndexByte(uname.Machine[:], 0)]),
		string(uname.Nodename[:bytes.IndexByte(uname.Nodename[:], 0)]),
	)
	return &metric, nil
}

// Collect implements the prometheus.Collector interface
func (s SmcCollector) Collect(ch chan<- prometheus.Metric) {
	keys := smc.GetSMCKeys()
	fltValues, uintValues, int_values := smc.GetKeyValues(keys)
	keysSeen := make([]string, 0, len(keys))

	for key, value := range fltValues {
		if value > 0 {
			processValue(s, ch, key, &keysSeen, value)
		}
	}

	for key, value := range uintValues {
		if value > 0 {
			processValue(s, ch, key, &keysSeen, value)
		}
	}

	for key, value := range int_values {
		if value > 0 {
			processValue(s, ch, key, &keysSeen, value)
		}
	}

	if info, err := s.getInfoMetric(); err == nil {
		ch <- *info
	}
}

func processValue[T Numeric](s SmcCollector, ch chan<- prometheus.Metric, key string, keysSeen *[]string, value T) {
	label := smc.GetSensorLabel(s.sensorLabels, key)
	seenKey := strings.ToUpper(key)
	fltVal := float64(value)

	batteryVoltageKeys := []string{"B0AV", "BC1V", "BC2V", "BC3V", "CHBV"}

	if slices.Contains(*keysSeen, seenKey) {
		level.Debug(s.logger).Log("msg", "duplicate key found", "key", seenKey, "label", label, "value", value)
	} else if label == "Unknown" {
		level.Debug(s.logger).Log("msg", "unknown sensor with non-negative value", "key", key, "value", value)
	} else if key == "B0TF" {
		// Special case: Average battery time to full. Units?
		*keysSeen = append(*keysSeen, seenKey)
		if fltVal == 0xffff {
			// 0xffff means fully charged
			fltVal = 0
		}
		ch <- prometheus.MustNewConstMetric(s.batteryChargeRemainMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key == "B0CT" {
		// Special case: Battery cycle count. Units: cycles
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.batteryCyclesMetric, prometheus.CounterValue, fltVal, seenKey, label)
	} else if key == "BFCL" {
		// Special case: Battery Final Charge Level. Units: %
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.batteryChargePctMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key[0] == 'T' {
		// Temperature sensor
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.tempMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key[0] == 'P' {
		// Power sensor
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.powerMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key[0] == 'V' || slices.Contains(batteryVoltageKeys, key) {
		if fltVal > 1000 {
			// Battery voltage may be in mV
			fltVal /= 1000
		}
		// Voltage sensor
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.voltageMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key[0] == 'I' || key == "B0AC" {
		if key == "B0AC" && fltVal > 1000 {
			// Battery current may be in mA
			fltVal /= 1000
		}
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.currentMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key[0] == 'F' {
		// Fan sensor
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.fanMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	} else if key[0] == 'B' {
		// Battery sensor
		*keysSeen = append(*keysSeen, seenKey)
		ch <- prometheus.MustNewConstMetric(s.batteryChargeMetric, prometheus.GaugeValue, fltVal, seenKey, label)
	}
}
