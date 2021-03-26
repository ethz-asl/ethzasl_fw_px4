/****************************************************************************
 *
 *   Copyright (c) 2017 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file si7210.cpp
 * Driver for the SI7210 connected via I2C.
 *
 * Author: Amir Melzer  <amir.melzer@mavt.ethz.ch>
 */

#include "si7210.hpp"

using namespace time_literals;

int
SI7210::probe()
{
	uint8_t reg;

	if (OK != get_regs(HREVID, &reg)) {
		// perf_count(_comms_errors);
		return -EIO;
	}

	if (((reg & 0xf0) >> 4) != IDCHIPID) {
		// perf_count(_comms_errors);
		return -EIO;
	}

	if ((reg & 0x0f) != REVID) {
		// perf_count(_comms_errors);
		return -EIO;
	}

	return OK;
}

/* Get registers value */
int
SI7210::get_regs(uint8_t ptr, uint8_t *regs)
{
	uint8_t data;

	if (OK != transfer(&ptr, 1, &data, 1)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	*regs  = data;

	return OK;
}

/* Set registers value */
int
SI7210::set_regs(uint8_t ptr, uint8_t value)
{
	uint8_t data[2];

	data[0] = ptr;
	data[1] = value;

	if (OK != transfer(&data[0], 2, nullptr, 0)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	/* read back the reg and verify */

	if (OK != transfer(&ptr, 1, &data[1], 1)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	if (data[1] != value) {
		//return -EIO;
	}

	return OK;
}

/* Get measurement value */
int
SI7210::get_measurement(uint8_t ptr, uint16_t *value)
{
	uint8_t data[2];

	if (OK != transfer(&ptr, 1, &data[0], 2)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	*value = (uint16_t)((data[1] & 0xff) + ((data[0] & 0xff) << 8));

	return OK;
}

/* Get sensor data */
int
SI7210::get_sensor_data(uint8_t otpAddr, int8_t *data)
{
	uint8_t optCtrl;
	uint8_t reg;

	if (OK != get_regs(OTP_CTRL, &optCtrl)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	if (OK != (optCtrl & OTP_CTRL_OPT_BUSY_MASK)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	reg = otpAddr;

	if (OK != set_regs(OTP_ADDR, reg)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	reg = OTP_CTRL_OPT_READ_EN_MASK;

	if (OK != set_regs(OTP_CTRL, reg)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	if (OK != get_regs(OTP_DATA, (uint8_t *) data)) {
		perf_count(_comms_errors);
		return -EIO;
	}

	return OK;
}


int SI7210::write_command(uint16_t command)
{
	uint8_t cmd[2];
	cmd[0] = static_cast<uint8_t>(command >> 8);
	cmd[1] = static_cast<uint8_t>(command & 0xff);
	return transfer(&cmd[0], 2, nullptr, 0);
}

bool
SI7210::init_si7210()
{
	return configure() == 0;
}

int
SI7210::configure()
{
	// int ret = write_command(SDP3X_CONT_MEAS_AVG_MODE);

	// if (ret != PX4_OK) {
	// 	perf_count(_comms_errors);
	// 	DEVICE_DEBUG("config failed");
	// 	_state = State::RequireConfig;
	// 	return ret;
	// }

	_state = State::Configuring;
	int ret = PX4_OK; //TODO: Move configuration here
	return ret;
}

void SI7210::start()
{
	// make sure to wait 10ms after configuring the measurement mode
	ScheduleDelayed(10_ms);
}

int SI7210::collect()
{
	perf_begin(_sample_perf);

	_collect_phase = false;
	// bool si7210_notify = true;

	uint8_t     reg;

	uint16_t    magRaw;
	uint16_t    tempRaw;

	int8_t 		otpTempOffset;
	int8_t 		otpTempGain;

	/* capture the magnetic field measurements */
	reg = ARAUTOINC_ARAUTOINC_MASK;

	if (OK != set_regs(ARAUTOINC, reg)) {
		return -EIO;
	}

	reg = DSPSIGSEL_MAG_VAL_SEL;

	if (OK != set_regs(DSPSIGSEL, reg)) {
		return -EIO;
	}

	reg = POWER_CTRL_ONEBURST_MASK;

	if (OK != set_regs(POWER_CTRL, reg)) {
		return -EIO;
	}

	if (OK != get_measurement(DSPSIGM, &magRaw)) {
		return -EIO;
	}

	/* capture the temperate measurements */
	reg = DSPSIGSEL_TEMP_VAL_SEL;

	if (OK != set_regs(DSPSIGSEL, reg)) {
		return -EIO;
	}

	reg = POWER_CTRL_ONEBURST_MASK;

	if (OK != set_regs(POWER_CTRL, reg)) {
		return -EIO;
	}

	if (OK != get_measurement(DSPSIGM, &tempRaw)) {
		return -EIO;
	}

	if (OK != get_sensor_data(OTP_TEMP_OFFSET, &otpTempOffset)) {
		return -EIO;
	}

	if (OK != get_sensor_data(OTP_TEMP_GAIN, &otpTempGain)) {
		return -EIO;
	}

	float _tempOffset = (float)otpTempOffset / 16;
	float _tempGain = 1 + (float)otpTempGain / 2048;

	if (OK == ((magRaw & 0x8000) && (tempRaw & 0x8000))) {
		return -EIO;
	}

	sensor_hall_s report{};

	/* generate a new report */
	report.timestamp = hrt_absolute_time();
	report.instance = 0; //TODO: _instance
	report.mag_t = (float)(magRaw - 0xC000) * 0.00125F;
	report.temp_c = (float)((tempRaw & ~0x8000) >> 3);
	report.temp_c = _tempGain * (-3.83e-6F * report.temp_c * report.temp_c + 0.16094F * report.temp_c - 279.80F - 0.222F *
				     3.0F) +
			_tempOffset;

	// _reports->force(&prb);


	_vane_pub.publish(report);

	perf_end(_sample_perf);

	return OK;
}

void
SI7210::RunImpl()
{
	switch (_state) {
	case State::RequireConfig:
		if (configure() == PX4_OK) {
			ScheduleDelayed(10_ms);

		} else {
			// periodically retry to configure
			ScheduleDelayed(300_ms);
		}

		break;

	case State::Configuring:
		_state = State::Running;

		ScheduleDelayed(10_ms);
		break;

	case State::Running:
		int ret = collect();

		if (ret != 0 && ret != -EAGAIN) {
			_sensor_ok = false;
			DEVICE_DEBUG("measure error");
			_state = State::RequireConfig;
		}

		ScheduleDelayed(SI7210_CONVERSION_INTERVAL);
		break;
	}
}

bool SI7210::crc(const uint8_t data[], unsigned size, uint8_t checksum)
{
	uint8_t crc_value = 0xff;

	// calculate 8-bit checksum with polynomial 0x31 (x^8 + x^5 + x^4 + 1)
	for (unsigned i = 0; i < size; i++) {
		crc_value ^= (data[i]);

		for (int bit = 8; bit > 0; --bit) {
			if (crc_value & 0x80) {
				crc_value = (crc_value << 1) ^ 0x31;

			} else {
				crc_value = (crc_value << 1);
			}
		}
	}

	// verify checksum
	return (crc_value == checksum);
}
