// Variometer.ino
//

#include <DefaultSettings.h>
#include <SerialEx.h>
#include <I2CDevice.h>
#include <EEPROMDriver.h>
#include <GlobalConfig.h>
#include <IMUModule.h>
#include <VertVelocity.h>
#include <NmeaParserEx.h>
#include <InputKey.h>
#include <OutputKey.h>
#include <FunctionKey.h>
#include <FuncKeyParser.h>
#include <LEDFlasher.h>
#include <ToneGenerator.h>
#include <ToneFrequency.h>
#include <TonePlayer.h>
#include <VarioBeeper.h>
#include <VarioSentence.h>
#include <BluetoothMan.h>
#include <IGCLogger.h>
#include <BatteryVoltage.h>
#include <CommandParser.h>
#include <SensorReporter.h>
#include <AccelCalibrator.h>

#include <libmaple\nvic.h>


enum _DeviceMode
{
	DEVICE_MODE_VARIO = 0,		// (0)
	DEVICE_MODE_UMS,			// (1)
	DEVICE_MODE_CALIBRATION,	// (2)
	DEVICE_MODE_SHUTDOWN,		// (3)
};

enum _VarioMode
{
	VARIO_MODE_INIT = 0,		// (0)
	VARIO_MODE_LANDING,			// (1)
	VARIO_MODE_FLYING,			// (2)
//	VARIO_MODE_SHUTDOWN,		// (3)
};

enum _CalibrationMode
{
	CAL_MODE_INIT = 0,
	CAL_MODE_MEASURE_DELAY,
	CAL_MODE_MEASURE,
	CAL_MODE_COMPLETION,
	CAL_MODE_DONE,
};


//
//
//

void board_init();

void changeDeviceMode();

void setup_vario();
void loop_vario();

void setup_ums();
void loop_ums();

void setup_calibration();
void loop_calibration();

void setup_shutdown();
void loop_shutdown();

void processLowBattery();
void processShutdownInterrupt();
void processCommand();


//
//
//

uint8_t deviceMode = DEVICE_MODE_VARIO;

uint8_t	varioMode; 		// sub-mode of vario-mode
uint8_t	calibMode;		// sub-mode of calibration-mode

uint32_t varioTick;		// vario tick-count
uint32_t deviceTick;	// global tick-count

void (* loop_main)(void) = 0;

//
// declare I2C instance
//
// I2C1 is used by IMU : I2CDevice is proxy interface of I2C1
// I2C2 is used by EEPROM
//

HardWire Wire1(1, I2C_FAST_MODE);
HardWire Wire2(2, I2C_FAST_MODE);

// initialize some static member of I2CDevice(I2C1)
//

// set wire reference member to I2C1
HardWire & I2CDevice::Wire = Wire1;

// set unlock callback function
unlockCallback I2CDevice::cbUnlock = SensorMS5611::unlockI2C;

// declare EEPROMDriver
EEPROMDriver eeprom(Wire2);
//
GlobalConfig Config(eeprom, EEPROM_ADDRESS);

//
// InertialMeasurementUnit Module : measure accelerometer & gyro & do calibration for accelerometer
//    It internally uses I2CDevice 
//

IMUModule imu;

//
VertVelocity vertVel;


//
// declare Serial relative instance
//
// BT uses Serial1
// GPS uses Serial2

NmeaParserEx nmeaParser(SerialEx2);


//
//
//

VarioSentence varioNmea(VARIOMETER_DEFAULT_NMEA_SENTENCE);


//
//
//

SensorReporter sensorReporter;


//
//
//

BluetoothMan btMan(SerialEx1, nmeaParser, varioNmea, sensorReporter);


//
// IGC Logger
//

IGCLogger logger;


//
// Digital & Analog Input/Output
//

// generic digital input
InputKey keyMode;
InputKey keyShutdown;
InputKey keyUSB;
// functional input
FunctionKey	keyFunc;
// analog input
BatteryVoltage batVolt;

// generic digital output
OutputKey keyPowerGPS;
OutputKey keyPowerBT;
OutputKey keyPowerDev;
// functional output
LEDFlasher  ledFlasher;


//
//
//

ToneGenerator toneGen;
TonePlayer tonePlayer(toneGen);

VarioBeeper varioBeeper(tonePlayer);


//
//
//

CommandStack cmdStack;

CommandParser cmdParser1(CMD_FROM_USB, Serial, cmdStack); // USB serial parser
CommandParser cmdParser2(CMD_FROM_BT, Serial1, cmdStack); // BT serial parser
FuncKeyParser keyParser(keyFunc, cmdStack, tonePlayer);


//
//
//

AccelCalibrator accelCalibrator(imu);



//
//
//

void board_init()
{
	// Initialize Serials
	Serial.begin();
	//while (! Serial);
	
	Serial1.begin(BAUDRATE_BT); 	// Serial1(USART1) : for BT
	//while (! Serial1);
	
	Serial2.begin(BAUDRATE_GPS);	// Serial2(USART2) : for GPS
	//while (! Serial2);
	
	// Initialize I2C
	Wire1.begin();
	Wire2.begin();
	
	// input pins
	//keyMode.begin(PIN_MODE_SELECT, ACTIVE_LOW); // not used
	keyShutdown.begin(PIN_SHDN_INT, ACTIVE_LOW);
	keyUSB.begin(PIN_USB_DETECT, ACTIVE_HIGH);
	// function-key
	keyFunc.begin(PIN_FUNC_INPUT, ACTIVE_HIGH);
	// adc input
	batVolt.begin(PIN_ADC_BATTERY);	
	
	// output pins
	keyPowerGPS.begin(PIN_GPS_EN, ACTIVE_LOW, OUTPUT_INACTIVE);
	keyPowerBT.begin(PIN_BT_EN, ACTIVE_LOW, OUTPUT_INACTIVE);
	//keyPowerDev.begin(PIN_KILL_PWR, ACTIVE_LOW, OUTPUT_ACTIVE);
	// state beacon
	ledFlasher.begin(PIN_MCU_STATE, ACTIVE_LOW);
	ledFlasher.turnOn();
}


//
//
//

void changeDeviceMode(int mode)
{
	//
	if (deviceMode == DEVICE_MODE_VARIO)
	{
		// clean-up something
		//
		
		// turn-off GPS & BT
		keyPowerGPS.disable();
		keyPowerBT.disable();
		
		//
		if (logger.isLogging())
			logger.end();		
	}
	
	//
	switch ((deviceMode = mode))
	{
	case DEVICE_MODE_VARIO :
		setup_vario();
		loop_main = loop_vario;	
		break;
	case DEVICE_MODE_UMS :
		setup_ums();
		loop_main = loop_ums;
		break;
	case DEVICE_MODE_CALIBRATION :
		setup_calibration();
		loop_main = loop_calibration;
		break;
	case DEVICE_MODE_SHUTDOWN :
		setup_shutdown();
		loop_main = loop_shutdown;
		break;
	}
}


//
//
//

void setup()
{
	//
	board_init();	
	
	//
	Config.readAll();
	
	// ToneGenerator uses PIN_PWM_H(PA8 : Timer1, Channel1)
	toneGen.begin(PIN_PWM_H);
	
	//
	tonePlayer.setVolume(Config.vario_volume);	
		
	//
	changeDeviceMode(DEVICE_MODE_VARIO);
}


//
//
//

void loop()
{
	// main loop for each mode
	loop_main();

	
	//
	// common functions
	//
	
	// process command from serial or key
	processCommand();
	
	// low battery!!
	processLowBattery();
	
	// check shutdown interrupts and prepare shutdown
	processShutdownInterrupt();	
}


//
//
//

void setup_vario()
{
	//
	varioMode = VARIO_MODE_INIT;
	
	//
	logger.init();

	//
	imu.init();
	
	for (int i = 0; i < 100; i++)
	{
		while (! imu.dataReady());
		imu.updateData();
	}
	
	// initialize kalman filtered vertical velocity calculator
	vertVel.init(imu.getAltitude(), 
				imu.getVelocity(),
				Config.kalman_sigmaP, // POSITION_MEASURE_STANDARD_DEVIATION,
				Config.kalman_sigmaA, // ACCELERATION_MEASURE_STANDARD_DEVIATION,
				millis());		
	
	// turn-on GPS & BT
	keyPowerGPS.enable();
	keyPowerBT.enable();

	// led flash as init-state
	ledFlasher.blink(BTYPE_LONG_ON_SHORT_OFF);

	// start vario-loop
	//tonePlayer.setMelody(&startTone[0], sizeof(startTone) / sizeof(startTone[0]), 1, PLAY_PREEMPTIVE, KEY_VOLUME);
	tonePlayer.setBeep(NOTE_C4, 800, 500, 2, KEY_VOLUME);
	
	//
	deviceTick = millis();
}

void loop_vario()
{
	if (imu.dataReady())
	{
		//
		imu.updateData(/* &sensorReporter */);

		//
		vertVel.update(imu.getAltitude(), imu.getVelocity(), millis());

		//
		float velocity = vertVel.getVelocity();
		varioBeeper.setVelocity(velocity);
		Serial.println(velocity * 100.0, 2);
		
		float position = vertVel.getCalibratedPosition(); // vertVel.getPosition();
		logger.update(position);
		
		//
		{
			if (velocity < STABLE_SINKING_THRESHOLD || STABLE_CLIMBING_THRESHOLD < velocity)
				deviceTick = millis(); // reset tick because it's not quiet.
			
			if ((millis() - deviceTick) > AUTO_SHUTDOWN_THRESHOLD)
			{
				Serial.println("Now process auto-shutdown!!");
				
				changeDeviceMode(DEVICE_MODE_SHUTDOWN);
				return;
			}
		}
	}	
	
	// read & prase gps sentence
	nmeaParser.update();
	
	// update vario sentence periodically
	if (varioNmea.checkInterval())
		varioNmea.begin(vertVel.getPosition(), vertVel.getVelocity(), imu.getTemperature(), batVolt.getVoltage());

	// send any prepared sentence to BT
	btMan.update();	
	
	// beep beep beep!
	tonePlayer.update();
	
	// start voltage measurement periodically
	batVolt.update();
	
	// MCU State : LED Blinking
	ledFlasher.update();
	
	// IGC setence is available when it received a valid GGA. -> altitude is valid
	if (varioMode == VARIO_MODE_INIT  && nmeaParser.availableIGC())
	{
		// do position calibration
		vertVel.calibratePosition(nmeaParser.getAltitude());
		
		// now ready to fly~~~
		varioMode = VARIO_MODE_LANDING;

		//		
		ledFlasher.blink(BTYPE_LONG_ON_OFF);
		// play reday melody~~~
		//
	}
	else if (varioMode == VARIO_MODE_LANDING)
	{
		if (nmeaParser.getSpeed() > FLIGHT_START_MIN_SPEED)
		{
			//
			varioMode = VARIO_MODE_FLYING;
			
			ledFlasher.blink(BTYPE_SHORT_ON_OFF);
			// play take-off melody
			// ...
			
			// start logging & change mode
			logger.begin(nmeaParser.getDateTime());
			
			//
			varioTick = millis();
		}
	}
	else if (varioMode == VARIO_MODE_FLYING)
	{
		if (nmeaParser.getSpeed() < FLIGHT_START_MIN_SPEED)
		{
			if ((millis() - varioTick) > FLIGHT_LANDING_THRESHOLD)
			{
				//
				varioMode = VARIO_MODE_LANDING;
				
				//
				ledFlasher.blink(BTYPE_LONG_ON_OFF);
				// play landing melody
				// ...
				
				// stop logging & change mode
				logger.end();
			}
		}
		else
		{
			// reset varioTick
			varioTick = millis();
		}
	}
	
	// check logging state
	if (logger.isLogging())
	{
		// nmeaParser parses GPS sentence and converts it to IGC sentence
		
		//static unsigned long tick = millis();
		//static int index = 0;
		
		//if ((millis()-tick) > time_interval)
		{		
			while (nmeaParser.availableIGC())
				logger.write(nmeaParser.readIGC());
		}
	}	
}


//
//
//

void setup_ums()
{
	//
	ledFlasher.blink(BTYPE_BLINK_2_LONG_ON);
}

void loop_ums()
{
	ledFlasher.update();
	tonePlayer.update();
}

//
//
//

#define HIGH_BEEP_FREQ 			(1000)
#define LOW_BEEP_FREQ 			(100)
#define BASE_BEEP_DURATION 		(100)

#define MEASURE_DELAY 			(3000)

void setup_calibration()
{
	//
	calibMode = CAL_MODE_INIT;
	
	//
	accelCalibrator.init();
	
	//
	ledFlasher.blink(BTYPE_BLINK_3_LONG_ON);
	tonePlayer.setBeep(HIGH_BEEP_FREQ, BASE_BEEP_DURATION * 2, BASE_BEEP_DURATION, 3, KEY_VOLUME);
	
	calibMode = CAL_MODE_MEASURE_DELAY;
	deviceTick = millis();
}

void loop_calibration()
{
	//
	ledFlasher.update();
	tonePlayer.update();
	keyFunc.update();
	
	//
	if (calibMode == CAL_MODE_MEASURE_DELAY)
	{
		if (millis() - deviceTick > MEASURE_DELAY)
			calibMode = CAL_MODE_MEASURE;
	}
	else if (calibMode == CAL_MODE_MEASURE)
	{
		// make measure
		accelCalibrator.measure();
		
		// get orientation
		int orient = accelCalibrator.getMeasureOrientation();
		
		// push measure
		boolean measureValid = accelCalibrator.pushMeasure();

		// make corresponding beep
		if (measureValid)
			tonePlayer.setBeep(HIGH_BEEP_FREQ, BASE_BEEP_DURATION * 6, BASE_BEEP_DURATION * 3, 1, KEY_VOLUME);
		else 
			tonePlayer.setBeep(LOW_BEEP_FREQ, BASE_BEEP_DURATION * 6, BASE_BEEP_DURATION * 3, 1, KEY_VOLUME);	
		
		//
		if( accelCalibrator.canCalibrate() )
		{
			// calibrate & save result
			accelCalibrator.calibrate();

			// play completion melody & confirm
			calibMode = CAL_MODE_COMPLETION;
			
			tonePlayer.setBeep(HIGH_BEEP_FREQ, BASE_BEEP_DURATION * 2, BASE_BEEP_DURATION, 3, KEY_VOLUME);
			ledFlasher.blink(BTYPE_SHORT_ON_OFF);
		}
		else
		{
			// go back measure delay
			calibMode = CAL_MODE_MEASURE_DELAY;
			// reset delay tick
			deviceTick = millis();
		}
	}
	else if (calibMode == CAL_MODE_COMPLETION)
	{
		if (millis() - deviceTick > MEASURE_DELAY)
		{
			// jobs done. reset now!
			tonePlayer.setTone(LOW_BEEP_FREQ, KEY_VOLUME);
			delay(BASE_BEEP_DURATION * 4);
			
			// reset!!
			nvic_sys_reset();
			while(1);
		}				
	}
}


//
//
//

void setup_shutdown()
{
	//
	ledFlasher.blink(BTYPE_BLINK_3_LONG_OFF);
	tonePlayer.setBeep(NOTE_C3, 800, 800);

	//
	if (logger.isLogging())
		logger.end();
	
	//
	deviceTick = millis();
}

void loop_shutdown()
{
	if (millis() - deviceTick > SHUTDOWN_HOLD_TIME)
	{
		tonePlayer.setMute();
		keyPowerDev.begin(PIN_KILL_PWR, ACTIVE_LOW, OUTPUT_ACTIVE);
		
		while(1);
	}
}


//
// process low battery
//

void processLowBattery()
{
	#if 0
	if (deviceMode != DEVICE_MODE_SHUTDOWN)
	{
		if (batVolt.getVoltage() < LOW_BATTERY_THRESHOLD)
		{
			Serial.println("!!Alert!!");
			Serial.println("It's low battery. Device will be shutdown now!!");

			changeDeviceMode(DEVICE_MODE_SHUTDOWN);
		}
	}
	#endif
}


//
// process shutdown interrupt (from LTC2950)
//

void processShutdownInterrupt()
{
	if (keyShutdown.read() == INPUT_ACTIVE)
	{
		// shutdown interrupt trigged by LTC2950
		// clean-up & wait power-off(LTC2750 will turn off power)
		if (logger.isLogging())
			logger.end();
		
		// beep~
		//tonePlayer.setBeep(420, 0, 0, KEY_VOLUME);
		tonePlayer.setTone(360, KEY_VOLUME);
		while(1)
			tonePlayer.update();
	}
}


//
// process command from serial or key
//

void processCommand()
{
	//
	cmdParser1.update();
	cmdParser2.update();
	keyParser.update();
	
	//
	while(cmdStack.getSize())
	{
		Command cmd = cmdStack.dequeue();
		
		switch(cmd.code)
		{
		case CMD_MODE_SWITCH 	:
			// change current mode
			if (deviceMode != cmd.param)
			{
				switch (cmd.param)
				{
				case PARAM_SW_ICALIBRATION :
					// setup
					//setup_calibration();
					// loop
					//main_loop = icalibration_loop();
					return;
				case PARAM_SW_CALIBRATION  :
					changeDeviceMode(DEVICE_MODE_CALIBRATION);
					return;
				#ifdef FEATURE_IGCLOGGING_ENALBE
				case PARAM_SW_UMS          :
					if (keyUSB.read() && logger.isInitialized())
					{
						changeDeviceMode(DEVICE_MODE_UMS);
						return;
					}
					// else cann't change mode : warning beep~~
					break;
				#endif // FEATURE_IGCLOGGING_ENALBE
				}
			}
			break;
		case CMD_DEVICE_STATUS 	:
			break;
		case CMD_SENSOR_DUMP 	:
			btMan.blockSensorData(cmd.param);
			break;
		case CMD_NMEA_SENTENCE  :
			btMan.blockNmeaSentence(cmd.param);
			break;
		case CMD_TONE_TEST 		:
			break;
		case CMD_SOUND_LEVEL 	:
			switch (cmd.param)
			{
			case PARAM_LV_LOUD 	:
				tonePlayer.setVolume(MAX_VOLUME);
				tonePlayer.setBeep(460, 800, 400, 3);
				break;
			case PARAM_LV_QUIET :
				tonePlayer.setVolume(MID_VOLUME);
				tonePlayer.setBeep(460, 800, 400, 3);
				break;
			case PARAM_LV_MUTE	:
			default				: 
				tonePlayer.setVolume(MIN_VOLUME);
				break;
			}

			// save volume
			//Config.vario_volume = tonePlayer.getVolume();
			//Config.writeVarioVolume();
			Config.updateVarioVolume(tonePlayer.getVolume());
			break;
		case CMD_DEVICE_RESET 	:
			break;
		case CMD_QUERY_PARAM 	:
			switch (cmd.param)
			{
			case PARAM_QU_VARIO_SINK_THRESHOLD	: // (1)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.vario_sinkThreshold);
				break;
			case PARAM_QU_VARIO_CLIMB_THRESHOLD	: // (2)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.vario_climbThreshold);
				break;
			case PARAM_QU_VARIO_SENSITIVITY		: // (3)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.vario_sensitivity);
				break;
			//case PARAM_QU_NMEA_SENTENCE			: // (4)
			//	Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.vario_sentence);
			//	break;
			case PARAM_QU_TIME_ZONE             : // (5)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.vario_timezone);
				break;
			case PARAM_QU_KALMAN_SIGMA_P		: // (6)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.kalman_sigmaP);
				break;
			case PARAM_QU_KALMAN_SIGMA_A		: // (7)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.kalman_sigmaA);
				break;
			case PARAM_QU_CALIBRATION_X			: // (8)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.accel_calData[0]);
				break;
			case PARAM_QU_CALIBRATION_Y			: // (9)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.accel_calData[1]);
				break;
			case PARAM_QU_CALIBRATION_Z			: // (10)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.accel_calData[2]);
				break;
			case PARAM_QU_PROFILE_MODEL			: // (11)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.profile_model);
				break;
			case PARAM_QU_PROFILE_PILOT			: // (12)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.profile_pilot);
				break;
			case PARAM_QU_PROFILE_GLIDER		: // (13)
				Serial.print("%QU,"); Serial.print(cmd.param); Serial.print(","); Serial.println(Config.profile_glider);
				break;
			case PARAM_QU_TONE_TABLE			: // (14)
				break;
			default                             :
				Serial.println("!ERR");
				break;
			}
			break;
		case CMD_UPDATE_PARAM 	:
			break;
		}
	}	
}
