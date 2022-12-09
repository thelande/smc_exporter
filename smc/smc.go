package smc

/*
#cgo CFLAGS: -I../libsmc
#cgo LDFLAGS: -L../libsmc -lsmc -framework IOKit -framework CoreFoundation
#include "smc.h"
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>

SMCVal_t getKeyValue(const char *inKey) {
	SMCVal_t      val;
	UInt32Char_t  key = { 0 };
	strncpy(key, inKey, sizeof(key));
	key[sizeof(key) - 1] = '\0';
	SMCReadKey(key, &val);
	return val;
}
*/
import "C"
import (
	"bufio"
	"encoding/json"
	"os"
	"strings"
	"unsafe"
)

type SensorLabels struct {
	Labels map[string][]string `json:"labels"`
}

func GetAllSensorLabels(filename string) map[string][]string {
	var labels SensorLabels
	f, err := os.Open(filename)
	if err != nil {
		panic(err)
	}
	defer f.Close()

	if err := json.NewDecoder(f).Decode(&labels); err != nil {
		panic(err)
	}

	return labels.Labels
}

func GetSensorLabel(allLabels map[string][]string, key string) string {
	if labels, ok := allLabels[key]; ok {
		return labels[0]
	}
	// if line, found := CheckForLabel(key); found {
	// 	fmt.Printf("Found label line for key: %s\n", line)
	// }
	return "Unknown"
}

func GetSMCKeyCount() uint32 {
	C.smc_init()
	v := uint32(C.SMCReadIndexCount())
	C.smc_close()
	return v
}

func GetSMCKeys() []string {
	keyCount := GetSMCKeyCount()
	keys := make([]string, keyCount)

	C.smc_init()
	for i := 0; i < int(keyCount); i++ {
		name := C.SMCGetKeyName(C.int(i))
		keys[i] = C.GoString(name)
		defer C.free(unsafe.Pointer(name))
	}
	C.smc_close()

	return keys
}

func GetKeyValues(keys []string) (map[string]float32, map[string]uint, map[string]int) {
	fltValues := make(map[string]float32)
	uintValues := make(map[string]uint)
	intValues := make(map[string]int)

	C.smc_init()
	for _, key := range keys {
		val := C.getKeyValue(C.CString(key))
		if int(C.valIsFloat(val)) != 0 {
			fltValues[key] = float32(C.getVal(val))
		} else if int(C.valIsUInt(val)) != 0 {
			uintValues[key] = uint(C.getUIntVal(val))
		} else if int(C.valIsInt(val)) != 0 {
			intValues[key] = int(C.getIntVal(val))
		}
	}
	C.smc_close()

	return fltValues, uintValues, intValues
}

/*
 * Checks if the given key exists in the list of all keys. If it does, return
 * the line it was found in.
 */
func CheckForLabel(key string) (string, bool) {
	f, err := os.Open("sensorkeys.txt")
	if err != nil {
		panic(err)
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.Contains(line, key) {
			return line, true
		}
	}

	return "", false
}
