/*
 * Lua RTOS, Lua sensor module
 *
 * Copyright (C) 2015 - 2016
 * IBEROXARXA SERVICIOS INTEGRALES, S.L. & CSS IBÉRICA, S.L.
 *
 * Author: Jaume Olivé (jolive@iberoxarxa.com / jolive@whitecatboard.org)
 *
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#include "luartos.h"

#if LUA_USE_SENSOR

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "auxmods.h"
#include "error.h"
#include "sensor.h"
#include "modules.h"

#include <stdio.h>
#include <string.h>

#include <drivers/sensor.h>
#include <sensors/ds1820.h>
#include <sensors/bme280.h>

// This variables are defined at linker time
extern LUA_REG_TYPE sensor_error_map[];
extern const sensor_t sensors[];

static void lsensor_setup_prepare( lua_State* L, const sensor_t *sensor, sensor_setup_t *setup ) {
	switch (sensor->interface) {
		case ADC_INTERFACE:
			setup->adc.unit = luaL_checkinteger(L, 2);
			setup->adc.channel = luaL_checkinteger(L, 3);
			setup->adc.resolution = luaL_checkinteger(L, 4);
			break;

		//case SPI_INTERFACE:
		case I2C_INTERFACE:
			setup->i2c.id = luaL_checkinteger(L, 2);
			setup->i2c.speed = luaL_checkinteger(L, 3);
			setup->i2c.sda = luaL_checkinteger(L, 4);
			setup->i2c.scl = luaL_checkinteger(L, 5);
			break;

		case OWIRE_INTERFACE:
			setup->owire.gpio = luaL_checkinteger(L, 2);
			setup->owire.owsensor = luaL_checkinteger(L, 3);
			break;

		case GPIO_INTERFACE:
			setup->gpio.gpio = luaL_checkinteger(L, 2);
			break;

		default:
			break;
	}
}

static int lsensor_set_prepare( lua_State* L, const sensor_t *sensor, const char *id, sensor_value_t *setting_value ) {
	// Initialize setting_value
	memset(setting_value, 0, sizeof(sensor_value_t));

	// Get sensor setting
	const sensor_data_t *setting = sensor_get_setting(sensor, id);

	if (!setting) {
		return luaL_exception(L, SENSOR_ERR_NOT_FOUND);
	}

	switch (setting->type) {
		case SENSOR_DATA_INT:
			setting_value->type = SENSOR_DATA_INT;
			setting_value->integerd.value = luaL_checkinteger(L, 3);
			break;

		case SENSOR_DATA_FLOAT:
			setting_value->type = SENSOR_DATA_FLOAT;
			setting_value->floatd.value   = luaL_checknumber(L, 3 );
			break;

		case SENSOR_DATA_DOUBLE:
			setting_value->type = SENSOR_DATA_DOUBLE;
			setting_value->doubled.value  = luaL_checknumber(L, 3 );
			break;

		default:
			break;
	}

	return 0;
}

static int read_exfunc( lua_State* L, sensor_instance_t *unit, sensor_exfunc_t value) {
	char buf[32] = {0};

	switch (value) {
		case EXFUNC_DS1820_GETROM:
			ds1820_getrom(unit, buf);
			lua_pushstring(L, buf);
			return 1;
			break;

		case EXFUNC_DS1820_GETTYPE:
			ds1820_gettype(unit, buf);
			lua_pushstring(L, buf);
			return 1;
			break;

		case EXFUNC_DS1820_GETRESOLUTION:
			lua_pushinteger(L, ds1820_get_res(unit));
			return 1;
			break;

		case EXFUNC_DS1820_NUMDEV:
			lua_pushinteger(L, ds1820_numdev(unit));
			return 1;
			break;

		case EXFUNC_OWIRE_LISTDEV:
			ow_list(unit);
			break;

		case EXFUNC_BME280_GETMODE:
			bm280_get_mode(unit, buf);
			lua_pushstring(L, buf);
			return 1;
			break;

		default:
			break;
	}
	return 0;
}

static int lsensor_setup( lua_State* L ) {
	driver_error_t *error;
	const sensor_t *sensor;
	sensor_instance_t *instance = NULL;
	sensor_setup_t setup;

    const char *id = luaL_checkstring( L, 1 );

	// Get sensor definition
	sensor = sensor_get(id);
	if (!sensor) {
    	return luaL_exception(L, SENSOR_ERR_NOT_FOUND);
	}

	// Prepare setup
	lsensor_setup_prepare(L, sensor, &setup);

	// Setup sensor
	if ((error = sensor_setup(sensor, &setup, &instance))) {
    	return luaL_driver_error(L, error);
    }

	// Create user data
    sensor_userdata *data = (sensor_userdata *)lua_newuserdata(L, sizeof(sensor_userdata));
    if (!data) {
    	return luaL_exception(L, SENSOR_ERR_NOT_ENOUGH_MEMORY);
    }

    data->instance = instance;

    luaL_getmetatable(L, "sensor");
    lua_setmetatable(L, -2);

    return 1;
}

static int lsensor_set( lua_State* L ) {
    sensor_userdata *udata = NULL;
	driver_error_t *error;
	sensor_value_t setting_value;
	int ret;

	udata = (sensor_userdata *)luaL_checkudata(L, 1, "sensor");
    luaL_argcheck(L, udata, 1, "sensor expected");

    const char *setting = luaL_checkstring( L, 2 );

    // Prepare setting value
    if ((ret = lsensor_set_prepare(L, udata->instance->sensor, setting, &setting_value))) {
    	return ret;
    }

    // Set sensor
	if ((error = sensor_set(udata->instance, setting, &setting_value))) {
    	return luaL_driver_error(L, error);
    }

    return 0;
}

static int lsensor_acquire( lua_State* L ) {
    sensor_userdata *udata = NULL;
	driver_error_t *error;

	udata = (sensor_userdata *)luaL_checkudata(L, 1, "sensor");
    luaL_argcheck(L, udata, 1, "sensor expected");

    // Acquire data from sensor
    if ((error = sensor_acquire(udata->instance))) {
    	return luaL_driver_error(L, error);
    }

	return 0;
}

static int lsensor_read( lua_State* L ) {
    sensor_userdata *udata = NULL;
	driver_error_t *error;
	sensor_value_t *value;

	udata = (sensor_userdata *)luaL_checkudata(L, 1, "sensor");
    luaL_argcheck(L, udata, 1, "sensor expected");

    const char *id = luaL_checkstring( L, 2 );

    // Read data
    if ((error = sensor_read(udata->instance, id, &value))) {
    	return luaL_driver_error(L, error);
    }

	switch (value->type) {
		case SENSOR_NO_DATA:
			lua_pushnil(L);
			return 1;

		case SENSOR_DATA_INT:
			lua_pushinteger(L, value->integerd.value);
			return 1;

		case SENSOR_DATA_FLOAT:
			lua_pushnumber(L, value->floatd.value);
			return 1;

		case SENSOR_DATA_DOUBLE:
			lua_pushnumber(L, value->doubled.value);
			return 1;

		case SENSOR_DATA_EXFUNC:
			return read_exfunc(L, udata->instance, (sensor_exfunc_t)value->exfuncd.value);
	}

	return 0;
}

static int lsensor_list( lua_State* L ) {
	const sensor_t *csensor = sensors;

	uint16_t count = 0, i = 0, idx, len;
	uint8_t table = 0;
	char interface[7];
	char type[7];
	uint16_t pmax_len = 0;
	uint16_t smax_len = 0;
	uint16_t p_len = 0;
	uint16_t s_len = 0;
	uint16_t row_len = 0;

	// Check if user wants result as a table, or wants result
	// on the console
	if (lua_gettop(L) == 1) {
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		if (lua_toboolean(L, 1)) {
			table = 1;
		}
	}

	if (!table) {
		//printf("SENSOR      INTERFACE   PROVIDES                    SETTINGS                   \r\n");
		//printf("-------------------------------------------------------------------------------\r\n");
		const sensor_t *csensor1 = sensors;
		while (csensor1->id) {
			p_len = 0;
			s_len = 0;
			for(idx=0; idx < SENSOR_MAX_DATA; idx++) {
				if (csensor1->data[idx].id) {
					if (p_len > 0) p_len += 1;
					p_len += strlen(csensor1->data[idx].id);
				}
			}
			for(idx=0; idx < SENSOR_MAX_SETTINGS; idx++) {
				if (csensor1->settings[idx].id) {
					if (s_len > 0) s_len += 1;
					s_len += strlen(csensor1->settings[idx].id);
				}
			}
			if (pmax_len < p_len) pmax_len = p_len;
			if (smax_len < s_len) smax_len = s_len;
			csensor1++;
		}
		row_len += 24 + pmax_len + 3 + smax_len;
		printf("%-12s%-12s%*s%*s\r\n","SENSOR", "INTERFACE", (pmax_len+3)*-1, "PROVIDES", smax_len*-1, "SETTINGS");
		for (uint16_t n=0;n<row_len;n++) {
			printf("-");
		}
		printf("\r\n");
	} else {
		lua_createtable(L, count, 0);
	}

	while (csensor->id) {
		switch (csensor->interface) {
			case ADC_INTERFACE:   strcpy(interface, "ADC"); break;
			case SPI_INTERFACE:   strcpy(interface, "SPI"); break;
			case I2C_INTERFACE:   strcpy(interface, "I2C"); break;
			case OWIRE_INTERFACE: strcpy(interface, "1-WIRE"); break;
			case GPIO_INTERFACE:  strcpy(interface, "GPIO"); break;
			default:
				break;
		}

		if (!table) {
			printf("%-10s  %-9s   ",csensor->id, interface);
			len = 0;
			for(idx=0; idx < SENSOR_MAX_DATA; idx++) {
				if (csensor->data[idx].id) {
					if (len > 0) {
						printf(",");
						len += 1;
					}

					printf("%s", csensor->data[idx].id);
					len += strlen(csensor->data[idx].id);
				}
			}

			for(;len < pmax_len;len++) printf(" ");

			printf("   ");

			len = 0;
			for(idx=0; idx < SENSOR_MAX_SETTINGS; idx++) {
				if (csensor->settings[idx].id) {
					if (len > 0) {
						printf(",");
						len += 1;
					}

					printf("%s", csensor->settings[idx].id);
					len += strlen(csensor->settings[idx].id);
				}
			}

			printf("\r\n");
		} else {
			lua_pushinteger(L, i);

			lua_createtable(L, 0, 3);

	        lua_pushstring(L, (char *)csensor->id);
	        lua_setfield (L, -2, "id");

	        lua_pushstring(L, (char *)interface);
	        lua_setfield (L, -2, "interface");

	        lua_createtable(L, 0, 0);
	        for(idx=0; idx < SENSOR_MAX_DATA; idx++) {
				if (csensor->data[idx].id) {
					lua_pushinteger(L, idx);
					lua_createtable(L, 0, 2);

					lua_pushstring(L, (char *)csensor->data[idx].id);
			        lua_setfield (L, -2, "id");

			    	switch (csensor->data[idx].type) {
			    		case SENSOR_DATA_INT: strcpy(type, "int"); break;
			    		case SENSOR_DATA_FLOAT: strcpy(type, "float"); break;
			    		case SENSOR_DATA_DOUBLE: strcpy(type, "double"); break;
			    		case SENSOR_DATA_EXFUNC: strcpy(type, "exfunc"); break;

			    		default:
			    			break;
			    	}

			    	lua_pushstring(L, type);
			        lua_setfield (L, -2, "type");

			        lua_settable(L,-3);
				}
			}
	        lua_setfield (L, -2, "provides");

	        lua_createtable(L, 0, 0);
	        for(idx=0; idx < SENSOR_MAX_SETTINGS; idx++) {
				if (csensor->settings[idx].id) {
					lua_pushinteger(L, idx);
					lua_createtable(L, 0, 2);

					lua_pushstring(L, (char *)csensor->settings[idx].id);
			        lua_setfield (L, -2, "id");

			    	switch (csensor->settings[idx].type) {
			    		case SENSOR_DATA_INT: strcpy(type, "int"); break;
			    		case SENSOR_DATA_FLOAT: strcpy(type, "float"); break;
			    		case SENSOR_DATA_DOUBLE: strcpy(type, "double"); break;

			    		default:
			    			break;
			    	}

			    	lua_pushstring(L, type);
			        lua_setfield (L, -2, "type");

			        lua_settable(L,-3);
				}
			}
	        lua_setfield (L, -2, "settings");

	        lua_settable(L,-3);
		}

		csensor++;
		i++;
	}

	if (!table) {
		for (uint16_t n=0;n<row_len;n++) {
			printf("-");
		}
		printf("\r\n\r\n");
	}

	return table;
}

// Destructor
static int lsensor_ins_gc (lua_State *L) {
    sensor_userdata *udata = NULL;

    udata = (sensor_userdata *)luaL_checkudata(L, 1, "sensor");
	if (udata) {
		if (strcmp(udata->instance->sensor->id, "BME280") == 0) {
			free(udata->instance->setup.i2c.userdata);
		}
		free(udata->instance);
	}

	return 0;
}

static int lsensor_index(lua_State *L);
static int lsensor_ins_index(lua_State *L);

static const LUA_REG_TYPE lsensor_map[] = {
    { LSTRKEY( "setup"  ),	LFUNCVAL( lsensor_setup  ) },
	{ LSTRKEY( "list"   ),	LFUNCVAL( lsensor_list   ) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lsensor_ins_map[] = {
	{ LSTRKEY( "acquire"   ),	LFUNCVAL( lsensor_acquire   ) },
  	{ LSTRKEY( "read"      ),	LFUNCVAL( lsensor_read 	    ) },
  	{ LSTRKEY( "set"       ),	LFUNCVAL( lsensor_set 	    ) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lsensor_constants_map[] = {
	{LSTRKEY("error"), 			 LROVAL( sensor_error_map )},
	{ LNILKEY, LNILVAL }
};

static const luaL_Reg lsensor_func[] = {
    { "__index", 	lsensor_index },
    { NULL, NULL }
};

static const luaL_Reg lsensor_ins_func[] = {
	{ "__gc"   , 	lsensor_ins_gc },
    { "__index", 	lsensor_ins_index },
    { NULL, NULL }
};

static int lsensor_index(lua_State *L) {
	return luaR_index(L, lsensor_map, lsensor_constants_map);
}

static int lsensor_ins_index(lua_State *L) {
	return luaR_index(L, lsensor_ins_map, NULL);
}

LUALIB_API int luaopen_sensor( lua_State *L ) {
    luaL_newlib(L, lsensor_func);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);

    luaL_newmetatable(L, "sensor");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_setfuncs(L, lsensor_ins_func, 0);
    lua_pop(L, 1);

    return 1;
}

MODULE_REGISTER_UNMAPPED(SENSOR, sensor, luaopen_sensor);

#endif

/*

s1 = sensor.setup("PING28015", pio.GPIO16)
s1:set("temperature",20)
while true do
	s1:acquire()
	distance = s1:read("distance")
	print("distance "..distance)
	tmr.delayms(500)
end

s1 = sensor.setup("TMP36", adc.ADC1, adc.ADC_CH6, 12)
while true do
	s1:acquire()
	temperature = s1:read("temperature")
	print("temp "..temperature)
	tmr.delayms(500)
end

s1 = sensor.setup("DHT11", pio.GPIO4)
while true do
	s1:acquire()
	temperature = s1:read("temperature")
	humidity = s1:read("humidity")
	print("temp "..temperature..", hum "..humidity)
	tmr.delayms(500)
end

s1 = sensor.setup("DS1820", pio.GPIO18, 1)
s1:set("resolution",11)
while true do
	s1:acquire()
	temperature = s1:read("temperature")
	print("temp "..temperature..")
	tmr.delayms(500)
end

s1 = sensor.setup("BME280", i2c.I2C0, 400, 21, 22)
while true do
	s1:acquire()
	temperature = s1:read("temperature")
	humidity = s1:read("humidity")
	pressure = s1:read("pressure")
	print("temp "..temperature..", hum "..humidity, pres "..pressure)
	tmr.delayms(500)
end

 */
