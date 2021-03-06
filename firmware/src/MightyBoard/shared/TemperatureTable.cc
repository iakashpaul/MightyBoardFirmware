/*
 * Copyright 2010 by Adam Mayer	 <adam@makerbot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "TemperatureTable.hh"
#include "Configuration.hh"
#include "EepromMap.hh"
#include <avr/eeprom.h>
#include <stdint.h>
#include <avr/pgmspace.h>


// TODO: Clean this up...
#if defined HAS_THERMISTOR_TABLES

// Thermistor lookup table for RepRap Temperature Sensor Boards (http://make.rrrf.org/ts)
// Made with createTemperatureLookup.py (http://svn.reprap.org/trunk/reprap/firmware/Arduino/utilities/createTemperatureLookup.py)
// ./createTemperatureLookup.py --r0=100000 --t0=25 --r1=0 --r2=4700 --beta=4066 --max-adc=1023
// r0: 100000
// t0: 25
// r1: 0
// r2: 4700
// beta: 4066
// max adc: 1023

#ifdef MODEL_REPLICATOR
const static Entry default_therm_table[] PROGMEM = {
	{1,	841},
	{54,	255},
	{107,	209},
	{160,	184},
	{213,	166},
	{266,	153},
	{319,	142},
	{372,	132},
	{425,	124},
	{478,	116},
	{531,	108},
	{584,	101},
	{637,	93},
	{690,	86},
	{743,	78},
	{796,	70},
	{849,	61},
	{902,	50},
	{955,	34},
	{1008,	3}
};
#else // MODEL_REPLICATOR2
const static Entry default_therm_table[] PROGMEM = {
	{1,	916},
	{54,	265},
	{107,	216},
	{160,	189},
	{213,	171},
	{266,	157},
	{319,	132}, // Temps above 135 will be invalid
	{372,	124},
	{425,	116},
	{478,	109},
	{531,	102},
	{584,	96},
	{637,	89},
	{690,	82},
	{743,	75},
	{796,	68},
	{849,	58},
	{902,	48},
	{955,	34},
	{1008,	2}
};
#endif

const static Entry thermocouple_lookup[] PROGMEM = {
	{-304,	-64},
	{-232,	-48},
	{-157,	-32},
	{-79,	-16},
	{0,	0},
	{81,	16},
	{164,	32},
	{248,	48},
	{320,	64},
	{418,	80},
	{503,	96},
	{587,	112},
	{671,	128},
	{754,	144},
	{837,	160},
	{919,	176},
	{1000,	192},
	{1082,	208},
	{1164,	224},
	{1247,	240},
	{1330,	256},
	{1414,	272},
	{1499,	288},
	{1583,	304},
	{1754,	336},
	{1840,	352},
	{1926,	368},
	{2012,	384},
	{2152,	400},
};


/// cold temperature lookup table provided by ADS1118 data sheet
const static Entry cold_temp_lookup[] PROGMEM = {
	{-1760,	-55},
	{-800,	-25},
	{-8,	-0.25},
	{0,	0},
	{8,	0.25},
	{800,	25},
	{1600,	50},
	{2400,	75},
	{2560,	80},
	{3200,	100},
	{4096,	128},
}; 

namespace TemperatureTable{

bool has_table[3] = {1,1,1};

/// get value from lookup tables stored in progmem
/// 
/// @param[in] entryIdx table entry offset to read
/// @param[in] table_id  which table to read (valid values defined by therm_table struct)
/// @return  table Entry, a pair of the format (adc_read, temperature) 
inline Entry getEntry(int8_t entryIdx, int8_t table_id) {
	Entry rv;
	if (has_table[table_id]) {
		
		// get from progmem
		switch(table_id){
			case table_thermistor:
				memcpy_PF(&rv, (uint_farptr_t)&(default_therm_table[entryIdx]), sizeof(Entry));
				break;
			case table_thermocouple:
				memcpy_PF(&rv, (uint_farptr_t)&(thermocouple_lookup[entryIdx]), sizeof(Entry));
				break;
			case table_cold_junction:
				memcpy_PF(&rv, (uint_farptr_t)&(cold_temp_lookup[entryIdx]), sizeof(Entry));
				break;
		}
	}
	return rv;
}

/// Translate a temperature reading into degrees Celcius, using the provided lookup table.
/// @param[in] reading Thermistor/Thermocouple voltage reading, in ADC counts
/// @param[in] table_idx therm_tables index of the temperature lookup table
/// @param[in] max_allowed_value default temperature if reading is outside of lookup table
/// @return Temperature reading, in degrees Celcius
int16_t TempReadtoCelsius(int16_t reading, int8_t table_idx, int16_t max_allowed_value) {
	int8_t bottom = 0;
	int8_t current_numtemps;
  current_numtemps = NUMTEMPS_ALL[table_idx];

	int8_t top = current_numtemps - 1;
	int8_t mid = (bottom+top)/2;
	int8_t t;
	Entry e;
	while (mid > bottom) {
		t = mid;
		e = getEntry(mid,table_idx);
		if (reading < e.adc) {
			top = mid;
			mid = (bottom+top)/2;
		} else {
			bottom = mid;
			mid = (bottom+top)/2;
		}
	}
	Entry eb = getEntry(bottom,table_idx);
	Entry et = getEntry(top,table_idx);
	if (bottom == 0 && reading < eb.adc) {
		// out of scale; safety mode
		return max_allowed_value;
	}
	if (top == current_numtemps-1 && reading > et.adc) {
		// out of scale; safety mode
		return max_allowed_value;
	}

	int16_t celsius  = eb.value +
		  ((reading - eb.adc) * (et.value - eb.value)) / (et.adc - eb.adc);
	if (celsius > max_allowed_value) {
		celsius = max_allowed_value;
	}
	return celsius;
}

}

#endif
