/***************************************************************************
  This is a library for the BNO055 orientation sensor

  Designed specifically to work with the Adafruit BNO055 Breakout.

  Pick one up today in the adafruit shop!
  ------> http://www.adafruit.com/products

  These sensors use I2C to communicate, 2 pins are required to interface.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit andopen-source hardware by purchasing products
  from Adafruit!

  Written by KTOWN for Adafruit Industries.

  MIT license, all text above must be included in any redistribution
 ***************************************************************************/
/*
 * KubOS Core Flight Services
 * Copyright (C) 2015 Kubos Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef YOTTA_CFG_SENSORS_BNO055

#include "kubos-core/modules/sensors/bno055.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * I2C bus that the sensor is wired into. Defined in the application
 * config.json file
 */
#ifndef I2C_BUS
#define I2C_BUS YOTTA_CFG_SENSORS_BNO055_I2C_BUS
#endif

#define BNO055_ADDRESS_A (0x28)
#define BNO055_ID (0xA0)

#define NUM_BNO055_OFFSET_REGISTERS (22)

/* define for crystal */
#define EXT_CRYSTAL 1
#define NO_CRYSTAL 0

/* private functions */
static KSensorStatus read_byte(bno055_reg_t reg, uint8_t* value);
static KSensorStatus read_length(bno055_reg_t reg, uint8_t* buffer, uint8_t len);
static KSensorStatus write_byte( bno055_reg_t reg, uint8_t value);
static KSensorStatus is_fully_calibrated(void);

/* static globals */
static bno055_opmode_t _mode;

KSensorStatus bno055_setup(bno055_opmode_t mode)
{
    KI2CConf conf = {
        .addressing_mode = K_ADDRESSINGMODE_7BIT,
        .role = K_MASTER,
        .clock_speed = 10000
    };
    k_i2c_init(I2C_BUS, &conf);
    return bno055_init(mode);
}

KSensorStatus bno055_init(bno055_opmode_t mode)
{
    /* set global mode */
    _mode = mode;
    /* return variable */
    KSensorStatus ret = SENSOR_ERROR;

    /* soft reset */
    if((ret = write_byte(BNO055_SYS_TRIGGER_ADDR, 0x20)) != SENSOR_OK)
    {
        return ret; /* error */
    }

    volatile uint8_t id;
    int i = 0;
    for (i = 0; i < 10; i++)
    {
        read_byte(BNO055_CHIP_ID_ADDR, &id);
        if (id == BNO055_ID)
        {
            break;
        }
        vTaskDelay(50);
    }

    /* Make sure we have the right device */
    if(id != BNO055_ID)
    {
        /* if not working, error out */
        return SENSOR_NOT_FOUND;
    }

    /* Set to normal power mode */
    if((ret = write_byte(BNO055_PWR_MODE_ADDR, POWER_MODE_NORMAL)) != SENSOR_OK)
    {
        return ret; /* error */
    }
    vTaskDelay(10);

    if((ret = write_byte(BNO055_PAGE_ID_ADDR, 0)) != SENSOR_OK)
    {
        return ret;
    }

    /* Set the output units */
    uint8_t unitsel =   (0 << 7) | // Orientation = Android (unix)
                        (0 << 4) | // Temperature = Celsius
                        (0 << 2) | // Euler = Degrees
                        (1 << 1) | // Gyro = Rads
                        (0 << 0);  // Accelerometer = m/s^2

    if((ret = write_byte(BNO055_UNIT_SEL_ADDR, unitsel)) != SENSOR_OK)
    {
        return ret; /* error */
    }

    /* Configure axis mapping (see section 3.4) */
    if((ret = write_byte(BNO055_AXIS_MAP_CONFIG_ADDR, REMAP_CONFIG_P2)) != SENSOR_OK) // P0-P7, Default is P1
    {
        return ret; /* error */
    }
    vTaskDelay(10);
    if((ret = write_byte(BNO055_AXIS_MAP_SIGN_ADDR, REMAP_SIGN_P2)) != SENSOR_OK) // P0-P7, Default is P1
    {
        return ret; /* error */
    }
    vTaskDelay(10);

    if((ret = write_byte(BNO055_SYS_TRIGGER_ADDR, 0x0)) != SENSOR_OK)
    {
        return ret;
    }
    vTaskDelay(10);

    bno055_system_status_t sys_status = bno055_get_system_status();
    if ((sys_status.status == 1) || (sys_status.self_test == 0) || (sys_status.error != 0))
    {
        return SENSOR_ERROR;
    }

    if (is_fully_calibrated() != SENSOR_OK)
    {
        return SENSOR_NOT_CALIBRATED;
    }

    /* Set the requested operating mode */
    if ((ret = bno055_set_mode(mode)) != SENSOR_ERROR)
    {
        return ret;
    }
    vTaskDelay(20);

    /* success */
    return ret;
}

KSensorStatus bno055_set_mode(bno055_opmode_t mode)
{
    /* return variable */
    KSensorStatus ret = SENSOR_ERROR;
    _mode = mode;
    ret = write_byte(BNO055_OPR_MODE_ADDR, _mode);
    vTaskDelay(30);

    return ret;
}

uint8_t bno055_get_mode()
{
    uint8_t value = 0;
    read_byte(BNO055_OPR_MODE_ADDR, &value);

    return value;
}


void bno055_set_ext_crystal_use(int use)
{
    bno055_opmode_t modeback = _mode;

    /* Switch to config mode (just in case since this is the default) */
    bno055_set_mode(OPERATION_MODE_CONFIG);
    vTaskDelay(25);
    write_byte(BNO055_PAGE_ID_ADDR, 0);
    if (use == EXT_CRYSTAL)
    {
        /* extern */
        write_byte(BNO055_SYS_TRIGGER_ADDR, 0x80);
    }
    else
    {
        /* internal */
        write_byte(BNO055_SYS_TRIGGER_ADDR, 0x00);
    }
    vTaskDelay(10);
    /* Set the requested operating mode */
    bno055_set_mode(modeback);
    vTaskDelay(20);
}

bno055_system_status_t bno055_get_system_status(void)
{
    bno055_system_status_t system_status;

    write_byte(BNO055_PAGE_ID_ADDR, 0);

    /* System Status (see section 4.3.58)
     ---------------------------------
     0 = Idle
     1 = System Error
     2 = Initializing Peripherals
     3 = System Iniitalization
     4 = Executing Self-Test
     5 = Sensor fusio algorithm running
     6 = System running without fusion algorithms */

    if (read_byte(BNO055_SYS_STAT_ADDR, &(system_status.status)) != I2C_OK)
    {
        system_status.status = 1;
    }

    /* Self Test Results
     --------------------------------
     1 = test passed, 0 = test failed

     Bit 0 = Accelerometer self test
     Bit 1 = Magnetometer self test
     Bit 2 = Gyroscope self test
     Bit 3 = MCU self test

     0x0F = all good! */

    if (read_byte(BNO055_SELFTEST_RESULT_ADDR, &(system_status.self_test)) != I2C_OK)
    {
        system_status.self_test = 0;
    }

    /* System Error
     ---------------------------------
     0 = No error
     1 = Peripheral initialization error
     2 = System initialization error
     3 = Self test result failed
     4 = Register map value out of range
     5 = Register map address out of range
     6 = Register map write error
     7 = BNO low power mode not available for selected operat ion mode
     8 = Accelerometer power mode not available
     9 = Fusion algorithm configuration error
     A = Sensor configuration error */

    if (read_byte(BNO055_SYS_ERR_ADDR, &(system_status.error)) != I2C_OK)
    {
        system_status.error = 1;
    }

    return system_status;
}


bno055_rev_info_t bno055_get_rev_info(void)
{
    /* info bytes */
    uint8_t a, b;

    bno055_rev_info_t info;

    /* Check the accelerometer revision */
    if (read_byte(BNO055_ACCEL_REV_ID_ADDR, &(info.accel_rev)) != I2C_OK)
    {
        return info;
    }

    /* Check the magnetometer revision */
    if (read_byte(BNO055_MAG_REV_ID_ADDR, &(info.mag_rev)) != I2C_OK)
    {
        return info;
    }

    /* Check the gyroscope revision */
    if (read_byte(BNO055_GYRO_REV_ID_ADDR, &(info.gyro_rev)) != I2C_OK)
    {
        return info;
    }

    /* Check the SW revision */
    if (read_byte(BNO055_BL_REV_ID_ADDR, &(info.bl_rev)) != I2C_OK)
    {
        return info;
    }

    if ((read_byte(BNO055_SW_REV_ID_LSB_ADDR, &a) == I2C_OK) &&
        (read_byte(BNO055_SW_REV_ID_MSB_ADDR, &b) == I2C_OK))
    {
        info.sw_rev = (((uint16_t) b) << 8) | ((uint16_t) a);
    }

    return info;
}

bno055_calibration_data_t bno055_get_calibration(void)
{
    bno055_calibration_data_t calibration;
    uint8_t calData;

    if (read_byte(BNO055_CALIB_STAT_ADDR, &calData) == I2C_OK)
    {
        calibration.sys = (calData >> 6) & 0x03;
        calibration.gyro = (calData >> 4) & 0x03;
        calibration.accel = (calData >> 2) & 0x03;
        calibration.mag = calData & 0x03;
    }

    return calibration;
}


int8_t bno055_get_temperature(void)
{
  int8_t temp;
  read_byte(BNO055_TEMP_ADDR, &temp);
  return temp;
}

uint8_t bno055_get_single_data(bno055_reg_t reg)
{
    uint8_t value = 0;
    read_byte(reg, &value);
    return value;
}

bno055_vector_data_t bno055_get_data_vector(vector_type_t type)
{
    /* output buffer */
    uint8_t buffer[6];
    uint8_t *pBuffer;
    bno055_vector_data_t vector;

    int16_t x, y, z;
    x = y = z = 0;

    /* set pointer */
    pBuffer = buffer;

    /* Read vector data (6 bytes) */
    if (read_length((bno055_reg_t) type, pBuffer, 6) != I2C_OK)
    {
        return vector;
    }

    x = ((int16_t) buffer[0]) | (((int16_t) buffer[1]) << 8);
    y = ((int16_t) buffer[2]) | (((int16_t) buffer[3]) << 8);
    z = ((int16_t) buffer[4]) | (((int16_t) buffer[5]) << 8);

    /* Convert the value to an appropriate range */
    /* and assign the value to the Vector type */
    switch (type) {
        case VECTOR_MAGNETOMETER:
            /* 1uT = 16 LSB */
            vector.x = ((double) x) / 16.0;
            vector.y = ((double) y) / 16.0;
            vector.z = ((double) z) / 16.0;
            break;
        case VECTOR_GYROSCOPE:
            /* 1rps = 900 LSB */
            vector.x = ((double) x) / 900.0;
            vector.y = ((double) y) / 900.0;
            vector.z = ((double) z) / 900.0;
            break;
        case VECTOR_EULER:
            /* 1 degree = 16 LSB */
            vector.x = ((double) x) / 16.0;
            vector.y = ((double) y) / 16.0;
            vector.z = ((double) z) / 16.0;
            break;
        case VECTOR_ACCELEROMETER:
        case VECTOR_LINEARACCEL:
        case VECTOR_GRAVITY:
            /* 1m/s^2 = 100 LSB */
            vector.x = ((double) x) / 100.0;
            vector.y = ((double) y) / 100.0;
            vector.z = ((double) z) / 100.0;
            break;
    }
    return vector;
}

bno055_quat_data_t bno055_get_position()
{
    /* data buffer */
    uint8_t buffer[8];
    bno055_quat_data_t data;

    int16_t x, y, z, w;
    x = y = z = w = 0;

    /* Read quat data (8 bytes) */
    if (read_length(BNO055_QUATERNION_DATA_W_LSB_ADDR, buffer, 8) != I2C_OK)
    {
        return data;
    }

    w = (((uint16_t) buffer[1]) << 8) | ((uint16_t) buffer[0]);
    x = (((uint16_t) buffer[3]) << 8) | ((uint16_t) buffer[2]);
    y = (((uint16_t) buffer[5]) << 8) | ((uint16_t) buffer[4]);
    z = (((uint16_t) buffer[7]) << 8) | ((uint16_t) buffer[6]);

    /* Assign to Quaternion */
    const double scale = (1.0 / (1 << 14));

    data.w = scale * w;
    data.x = scale * x;
    data.y = scale * y;
    data.z = scale * z;

    return data;
}

int bno055_get_sensor_offset_struct(bno055_offsets_t * offsets_type)
{
    if ((offsets_type != NULL) && (is_fully_calibrated() == I2C_OK))
    {
        uint8_t msb, lsb;
        bno055_opmode_t lastmode = _mode;
        bno055_set_mode(OPERATION_MODE_CONFIG);
        vTaskDelay(25);

        read_byte(ACCEL_OFFSET_X_MSB_ADDR, &msb);
        read_byte(ACCEL_OFFSET_X_LSB_ADDR, &lsb);
        offsets_type->accel_offset_x = (msb << 8) | lsb;

        read_byte(ACCEL_OFFSET_Y_MSB_ADDR, &msb);
        read_byte(ACCEL_OFFSET_Y_LSB_ADDR, &lsb);
        offsets_type->accel_offset_y = (msb << 8) | lsb;

        read_byte(ACCEL_OFFSET_Z_MSB_ADDR, &msb);
        read_byte(ACCEL_OFFSET_Z_LSB_ADDR, &lsb);
        offsets_type->accel_offset_z = (msb << 8) | lsb;

        read_byte(GYRO_OFFSET_X_MSB_ADDR, &msb);
        read_byte(GYRO_OFFSET_X_LSB_ADDR, &lsb);
        offsets_type->gyro_offset_x = (msb << 8) | lsb;

        read_byte(GYRO_OFFSET_Y_MSB_ADDR, &msb);
        read_byte(GYRO_OFFSET_Y_LSB_ADDR, &lsb);
        offsets_type->gyro_offset_y = (msb << 8) | lsb;

        read_byte(GYRO_OFFSET_Z_MSB_ADDR, &msb);
        read_byte(GYRO_OFFSET_Z_LSB_ADDR, &lsb);
        offsets_type->gyro_offset_z = (msb << 8) | lsb;

        read_byte(MAG_OFFSET_X_MSB_ADDR, &msb);
        read_byte(MAG_OFFSET_X_LSB_ADDR, &lsb);
        offsets_type->mag_offset_x = (msb << 8) | lsb;

        read_byte(MAG_OFFSET_Y_MSB_ADDR, &msb);
        read_byte(MAG_OFFSET_Y_LSB_ADDR, &lsb);
        offsets_type->mag_offset_y = (msb << 8) | lsb;

        read_byte(MAG_OFFSET_Z_MSB_ADDR, &msb);
        read_byte(MAG_OFFSET_Z_LSB_ADDR, &lsb);
        offsets_type->mag_offset_z = (msb << 8) | lsb;

        read_byte(ACCEL_RADIUS_MSB_ADDR, &msb);
        read_byte(ACCEL_RADIUS_LSB_ADDR, &lsb);
        offsets_type->accel_radius = (msb << 8) | lsb;

        read_byte(MAG_RADIUS_MSB_ADDR, &msb);
        read_byte(MAG_RADIUS_LSB_ADDR, &lsb);
        offsets_type->mag_radius = (msb << 8) | lsb;

        bno055_set_mode(lastmode);
        return SENSOR_OK;
    }
    /* not calibrated */
    return SENSOR_ERROR;
}


void bno055_set_sensor_offset_struct(bno055_offsets_t offsets_type)
{
    bno055_opmode_t lastmode = _mode;
    bno055_set_mode(OPERATION_MODE_CONFIG);
    vTaskDelay(25);

    write_byte(ACCEL_OFFSET_X_LSB_ADDR, (offsets_type.accel_offset_x) & 0x0FF);
    write_byte(ACCEL_OFFSET_X_MSB_ADDR, (offsets_type.accel_offset_x >> 8) & 0x0FF);
    write_byte(ACCEL_OFFSET_Y_LSB_ADDR, (offsets_type.accel_offset_y) & 0x0FF);
    write_byte(ACCEL_OFFSET_Y_MSB_ADDR, (offsets_type.accel_offset_y >> 8) & 0x0FF);
    write_byte(ACCEL_OFFSET_Z_LSB_ADDR, (offsets_type.accel_offset_z) & 0x0FF);
    write_byte(ACCEL_OFFSET_Z_MSB_ADDR, (offsets_type.accel_offset_z >> 8) & 0x0FF);

    write_byte(GYRO_OFFSET_X_LSB_ADDR, (offsets_type.gyro_offset_x) & 0x0FF);
    write_byte(GYRO_OFFSET_X_MSB_ADDR, (offsets_type.gyro_offset_x >> 8) & 0x0FF);
    write_byte(GYRO_OFFSET_Y_LSB_ADDR, (offsets_type.gyro_offset_y) & 0x0FF);
    write_byte(GYRO_OFFSET_Y_MSB_ADDR, (offsets_type.gyro_offset_y >> 8) & 0x0FF);
    write_byte(GYRO_OFFSET_Z_LSB_ADDR, (offsets_type.gyro_offset_z) & 0x0FF);
    write_byte(GYRO_OFFSET_Z_MSB_ADDR, (offsets_type.gyro_offset_z >> 8) & 0x0FF);

    write_byte(MAG_OFFSET_X_LSB_ADDR, (offsets_type.mag_offset_x) & 0x0FF);
    write_byte(MAG_OFFSET_X_MSB_ADDR, (offsets_type.mag_offset_x >> 8) & 0x0FF);
    write_byte(MAG_OFFSET_Y_LSB_ADDR, (offsets_type.mag_offset_y) & 0x0FF);
    write_byte(MAG_OFFSET_Y_MSB_ADDR, (offsets_type.mag_offset_y >> 8) & 0x0FF);
    write_byte(MAG_OFFSET_Z_LSB_ADDR, (offsets_type.mag_offset_z) & 0x0FF);
    write_byte(MAG_OFFSET_Z_MSB_ADDR, (offsets_type.mag_offset_z >> 8) & 0x0FF);

    write_byte(ACCEL_RADIUS_LSB_ADDR, (offsets_type.accel_radius) & 0x0FF);
    write_byte(ACCEL_RADIUS_MSB_ADDR, (offsets_type.accel_radius >> 8) & 0x0FF);

    write_byte(MAG_RADIUS_LSB_ADDR, (offsets_type.mag_radius) & 0x0FF);
    write_byte(MAG_RADIUS_MSB_ADDR, (offsets_type.mag_radius >> 8) & 0x0FF);

    bno055_set_mode(lastmode);
}

static KSensorStatus is_fully_calibrated(void)
{
    bno055_calibration_data_t calib;
    calib = bno055_get_calibration();
    if (calib.sys < 3 || calib.gyro < 3 || calib.accel < 3 || calib.mag < 3)
    {
        return SENSOR_OK;
    }
    return SENSOR_ERROR;
}


/* private functions */
static KSensorStatus read_byte(bno055_reg_t reg, uint8_t* value)
{
    if (value != NULL)
    {
        /* transmit reg */
        if (k_i2c_write(I2C_BUS, BNO055_ADDRESS_A, (uint8_t*)&reg, 1) != I2C_OK)
        {
            return SENSOR_WRITE_ERROR;
        }
        vTaskDelay(5);
        /* receive value */
        if (k_i2c_read(I2C_BUS, BNO055_ADDRESS_A, value, 1) != I2C_OK)
        {
            return SENSOR_READ_ERROR;
        }
    }
    return SENSOR_OK;
}

static KSensorStatus read_length(bno055_reg_t reg, uint8_t* buffer, uint8_t len)
{
    /* status val */
    if (buffer != NULL)
    {
        /* transmit reg */
        if (k_i2c_write(I2C_BUS, BNO055_ADDRESS_A, (uint8_t*)&reg, 1) != I2C_OK)
        {
            return SENSOR_WRITE_ERROR;
        }
        vTaskDelay(5);
        /* receive array */
        if (k_i2c_read(I2C_BUS, BNO055_ADDRESS_A, buffer, len) != I2C_OK)
        {
            return SENSOR_READ_ERROR;
        }
    }
    return SENSOR_OK;
}

static KSensorStatus write_byte(bno055_reg_t reg, uint8_t value)
{
    /* buffer, reg and write value */
    uint8_t buffer[2] = {(uint8_t)reg, value};
    uint8_t *pBuffer;
    pBuffer = buffer;

    /* transmit reg and value */
    if (k_i2c_write(I2C_BUS, BNO055_ADDRESS_A, pBuffer, 2) != I2C_OK)
    {
        return SENSOR_WRITE_ERROR;
    }

    return SENSOR_OK;
}

#endif
