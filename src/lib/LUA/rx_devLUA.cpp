#ifdef TARGET_RX

#include "rxtx_devLua.h"
#include "helpers.h"
#include "devServoOutput.h"

extern void deferExecution(uint32_t ms, std::function<void()> f);
extern void reconfigureSerial();
extern bool BindingModeRequest;

static char modelString[] = "000";
#if defined(GPIO_PIN_PWM_OUTPUTS)
static char pwmModes[] = "50Hz;60Hz;100Hz;160Hz;333Hz;400Hz;10kHzDuty;On/Off;DShot;Serial RX;Serial TX;I2C SCL;I2C SDA";
#endif

static struct luaItem_selection luaSerialProtocol = {
    {"Protocol", CRSF_TEXT_SELECTION},
    0, // value
    "CRSF;Inverted CRSF;SBUS;Inverted SBUS;SUMD;DJI RS Pro;HoTT Telemetry",
    STR_EMPTYSPACE
};

static struct luaItem_selection luaFailsafeMode = {
    {"Failsafe Mode", CRSF_TEXT_SELECTION},
    0, // value
    "No Pulses;Last Pos",
    STR_EMPTYSPACE
};

#if defined(POWER_OUTPUT_VALUES)
static struct luaItem_selection luaTlmPower = {
    {"Tlm Power", CRSF_TEXT_SELECTION},
    0, // value
    strPowerLevels,
    "mW"
};
#endif

#if defined(GPIO_PIN_ANT_CTRL)
static struct luaItem_selection luaAntennaMode = {
    {"Ant. Mode", CRSF_TEXT_SELECTION},
    0, // value
    "Antenna A;Antenna B;Diversity",
    STR_EMPTYSPACE
};
#endif

// Gemini Mode
#if defined(GPIO_PIN_NSS_2)
static struct luaItem_selection luaDiversityMode = {
    {"Rx Mode", CRSF_TEXT_SELECTION},
    0, // value
    "Diversity;Gemini",
    STR_EMPTYSPACE
};
#endif

//----------------------------Info-----------------------------------

static struct luaItem_string luaModelNumber = {
    {"Model Id", CRSF_INFO},
    modelString
};

static struct luaItem_string luaELRSversion = {
    {version, CRSF_INFO},
    commit
};

//----------------------------Info-----------------------------------

//---------------------------- WiFi -----------------------------


//---------------------------- WiFi -----------------------------

//---------------------------- Output Mapping -----------------------------

#if defined(GPIO_PIN_PWM_OUTPUTS)
static struct luaItem_folder luaMappingFolder = {
    {"Output Mapping", CRSF_FOLDER},
};

static struct luaItem_int8 luaMappingChannelOut = {
  {"Output Ch", CRSF_UINT8},
  {
    {
      (uint8_t)5,       // value - start on AUX1, value is 1-16, not zero-based
      1,                // min
      PWM_MAX_CHANNELS, // max
    }
  },
  STR_EMPTYSPACE
};

static struct luaItem_int8 luaMappingChannelIn = {
  {"Input Ch", CRSF_UINT8},
  {
    {
      0,                 // value
      1,                 // min
      CRSF_NUM_CHANNELS, // max
    }
  },
  STR_EMPTYSPACE
};

static struct luaItem_selection luaMappingOutputMode = {
    {"Output Mode", CRSF_TEXT_SELECTION},
    0, // value
    pwmModes,
    STR_EMPTYSPACE
};

static struct luaItem_selection luaMappingInverted = {
    {"Invert", CRSF_TEXT_SELECTION},
    0, // value
    "Off;On",
    STR_EMPTYSPACE
};

static struct luaItem_command luaSetFailsafe = {
    {"Set Failsafe Pos", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

#endif // GPIO_PIN_PWM_OUTPUTS

//---------------------------- Output Mapping -----------------------------

static struct luaItem_selection luaVolatileBind = {
    {"Bind Storage", CRSF_TEXT_SELECTION},
    0, // value
    "Persistent;Volatile",
    STR_EMPTYSPACE
};

static struct luaItem_command luaBindMode = {
    {"Enter Bind Mode", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

#if defined(GPIO_PIN_PWM_OUTPUTS)
static void luaparamMappingChannelOut(struct luaPropertiesCommon *item, uint8_t arg)
{
    setLuaUint8Value(&luaMappingChannelOut, arg);

    // When the selected output channel changes, update the available PWM modes for that pin
    // Truncate the select options before the ; following On/Off
    pwmModes[50] = '\0';

#if defined(PLATFORM_ESP32)
    // DShot output (1 option)
    // ;DShot
    // ESP8266 enum skips this, so it is never present
    if (GPIO_PIN_PWM_OUTPUTS[arg-1] != 0)
    {
        strcat(pwmModes, ";DShot");
    }
    else
#endif
    {
        strcat(pwmModes, ";");
    }

    // SerialIO outputs (1 option)
    // ;[Serial RX] | [Serial TX]
    if (GPIO_PIN_PWM_OUTPUTS[arg-1] == 3)
    {
        strcat(pwmModes, ";Serial RX");
    }
    else if (GPIO_PIN_PWM_OUTPUTS[arg-1] == 1)
    {
        strcat(pwmModes, ";Serial TX");
    }
    else
    {
        strcat(pwmModes, ";");
    }

    // I2C pins (2 options)
    // ;[I2C SCL] ;[I2C SDA]
    // If the target defines SCL/SDA then those pins MUST be used,
    // otherwise allow any pin to be either SCL or SDA
    if (GPIO_PIN_PWM_OUTPUTS[arg-1] == GPIO_PIN_SCL)
    {
        strcat(pwmModes, ";I2C SCL;");
    }
    else if (GPIO_PIN_PWM_OUTPUTS[arg-1] == GPIO_PIN_SDA)
    {
        strcat(pwmModes, ";;I2C SDA");
    }
    else if (GPIO_PIN_SCL == UNDEF_PIN || GPIO_PIN_SDA == UNDEF_PIN)
    {
        strcat(pwmModes, ";I2C SCL;I2C SDA");
    }

    // trim off trailing semicolons (assumes pwmModes has at least 1 non-semicolon)
    for (auto lastPos = strlen(pwmModes)-1; pwmModes[lastPos] == ';'; lastPos--)
    {
        pwmModes[lastPos] = '\0';
    }

    // Trigger an event to update the related fields to represent the selected channel
    devicesTriggerEvent();
}

static void luaparamMappingChannelIn(struct luaPropertiesCommon *item, uint8_t arg)
{
  const uint8_t ch = luaMappingChannelOut.properties.u.value - 1;
  rx_config_pwm_t newPwmCh;
  newPwmCh.raw = config.GetPwmChannel(ch)->raw;
  newPwmCh.val.inputChannel = arg - 1; // convert 1-16 -> 0-15

  config.SetPwmChannelRaw(ch, newPwmCh.raw);
}

static void configureSerialPin(uint8_t sibling, uint8_t oldMode, uint8_t newMode)
{
  for (int ch=0 ; ch<GPIO_PIN_PWM_OUTPUTS_COUNT ; ch++)
  {
    if (GPIO_PIN_PWM_OUTPUTS[ch] == sibling)
    {
      // Retain as much of the sibling's current config as possible
      rx_config_pwm_t siblingPinConfig;
      siblingPinConfig.raw = config.GetPwmChannel(ch)->raw;

      // If the new mode is serial, the sibling is also forced to serial
      if (newMode == somSerial)
      {
        siblingPinConfig.val.mode = somSerial;
      }
      // If the new mode is not serial, and the sibling is serial, set the sibling to PWM (50Hz)
      else if (siblingPinConfig.val.mode == somSerial)
      {
        siblingPinConfig.val.mode = som50Hz;
      }

      config.SetPwmChannelRaw(ch, siblingPinConfig.raw);
      break;
    }
  }

  if (oldMode != newMode)
  {
    deferExecution(100, [](){
      reconfigureSerial();
    });
  }
}

static void luaparamMappingOutputMode(struct luaPropertiesCommon *item, uint8_t arg)
{
  UNUSED(item);
  const uint8_t ch = luaMappingChannelOut.properties.u.value - 1;
  rx_config_pwm_t newPwmCh;
  newPwmCh.raw = config.GetPwmChannel(ch)->raw;
  uint8_t oldMode = newPwmCh.val.mode;
  newPwmCh.val.mode = arg;

  // Check if pin == 1/3 and do other pin adjustment accordingly
  if (GPIO_PIN_PWM_OUTPUTS[ch] == 1)
  {
    configureSerialPin(3, oldMode, newPwmCh.val.mode);
  }
  else if (GPIO_PIN_PWM_OUTPUTS[ch] == 3)
  {
    configureSerialPin(1, oldMode, newPwmCh.val.mode);
  }
  config.SetPwmChannelRaw(ch, newPwmCh.raw);
}

static void luaparamMappingInverted(struct luaPropertiesCommon *item, uint8_t arg)
{
  UNUSED(item);
  const uint8_t ch = luaMappingChannelOut.properties.u.value - 1;
  rx_config_pwm_t newPwmCh;
  newPwmCh.raw = config.GetPwmChannel(ch)->raw;
  newPwmCh.val.inverted = arg;

  config.SetPwmChannelRaw(ch, newPwmCh.raw);
}

static void luaparamSetFalisafe(struct luaPropertiesCommon *item, uint8_t arg)
{
  luaCmdStep_e newStep;
  const char *msg;
  if (arg == lcsClick)
  {
    newStep = lcsAskConfirm;
    msg = "Set failsafe to curr?";
  }
  else if (arg == lcsConfirmed)
  {
    // This is generally not seen by the user, since we'll disconnect to commit config
    // and the handset will send another lcdQuery that will overwrite it with idle
    newStep = lcsExecuting;
    msg = "Setting failsafe";

    for (int ch=0; ch<GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
      rx_config_pwm_t newPwmCh;
      // The value must fit into the 10 bit range of the failsafe
      newPwmCh.raw = config.GetPwmChannel(ch)->raw;
      newPwmCh.val.failsafe = CRSF_to_UINT10(constrain(ChannelData[config.GetPwmChannel(ch)->val.inputChannel], CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX));
      //DBGLN("FSCH(%u) crsf=%u us=%u", ch, ChannelData[ch], newPwmCh.val.failsafe+988U);
      config.SetPwmChannelRaw(ch, newPwmCh.raw);
    }
  }
  else
  {
    newStep = lcsIdle;
    msg = STR_EMPTYSPACE;
  }

  sendLuaCommandResponse((struct luaItem_command *)item, newStep, msg);
}

#endif // GPIO_PIN_PWM_OUTPUTS

#if defined(POWER_OUTPUT_VALUES)

static void luaparamSetPower(struct luaPropertiesCommon* item, uint8_t arg)
{
  UNUSED(item);
  uint8_t newPower = arg + POWERMGNT::getMinPower();
  if (newPower > POWERMGNT::getMaxPower())
  {
    newPower = PWR_MATCH_TX;
  }

  config.SetPower(newPower);
  // POWERMGNT::setPower() will be called in updatePower() in the main loop
}

#endif // POWER_OUTPUT_VALUES

static void registerLuaParameters()
{
  registerLUAParameter(&luaSerialProtocol, [](struct luaPropertiesCommon* item, uint8_t arg){
    config.SetSerialProtocol((eSerialProtocol)arg);
    if (config.IsModified()) {
      deferExecution(100, [](){
        reconfigureSerial();
      });
    }
  });

  if (config.GetSerialProtocol() == PROTOCOL_SBUS || config.GetSerialProtocol() == PROTOCOL_INVERTED_SBUS || config.GetSerialProtocol() == PROTOCOL_DJI_RS_PRO)
  {
    registerLUAParameter(&luaFailsafeMode, [](struct luaPropertiesCommon* item, uint8_t arg){
      config.SetFailsafeMode((eFailsafeMode)arg);
    });
  }

  if (GPIO_PIN_ANT_CTRL != UNDEF_PIN)
  {
    registerLUAParameter(&luaAntennaMode, [](struct luaPropertiesCommon* item, uint8_t arg){
      config.SetAntennaMode(arg);
    });
  }

  // Gemini Mode
  if (isDualRadio())
  {
    registerLUAParameter(&luaDiversityMode, [](struct luaPropertiesCommon* item, uint8_t arg){
      config.SetAntennaMode(arg); // Reusing SetAntennaMode since both GPIO_PIN_ANTENNA_SELECT and GPIO_PIN_NSS_2 will not be defined together.
    });
  }

#if defined(POWER_OUTPUT_VALUES)
  luadevGeneratePowerOpts(&luaTlmPower);
  registerLUAParameter(&luaTlmPower, &luaparamSetPower);
#endif

#if defined(GPIO_PIN_PWM_OUTPUTS)
  if (OPT_HAS_SERVO_OUTPUT)
  {
    luaparamMappingChannelOut(&luaMappingOutputMode.common, luaMappingChannelOut.properties.u.value);
    registerLUAParameter(&luaMappingFolder);
    registerLUAParameter(&luaMappingChannelOut, &luaparamMappingChannelOut, luaMappingFolder.common.id);
    registerLUAParameter(&luaMappingChannelIn, &luaparamMappingChannelIn, luaMappingFolder.common.id);
    registerLUAParameter(&luaMappingOutputMode, &luaparamMappingOutputMode, luaMappingFolder.common.id);
    registerLUAParameter(&luaMappingInverted, &luaparamMappingInverted, luaMappingFolder.common.id);
    registerLUAParameter(&luaSetFailsafe, &luaparamSetFalisafe);
  }
#endif

  registerLUAParameter(&luaVolatileBind, [](struct luaPropertiesCommon* item, uint8_t arg) {
    config.SetVolatileBind(arg);
  });
  registerLUAParameter(&luaBindMode, [](struct luaPropertiesCommon* item, uint8_t arg){
    // Complete when TX polls for status i.e. going back to idle, because we're going to lose connection
    if (arg == lcsQuery) {
      deferExecution(200, [](){ BindingModeRequest = true; });
    }
    sendLuaCommandResponse(&luaBindMode, arg < 5 ? lcsExecuting : lcsIdle, arg < 5 ? "Entering..." : "");
  });

  registerLUAParameter(&luaModelNumber);
  registerLUAParameter(&luaELRSversion);
  registerLUAParameter(nullptr);
}

static int event()
{
  setLuaTextSelectionValue(&luaSerialProtocol, config.GetSerialProtocol());
  setLuaTextSelectionValue(&luaFailsafeMode, config.GetFailsafeMode());

  if (GPIO_PIN_ANT_CTRL != UNDEF_PIN)
  {
    setLuaTextSelectionValue(&luaAntennaMode, config.GetAntennaMode());
  }

  // Gemini Mode
  if (isDualRadio())
  {
    setLuaTextSelectionValue(&luaDiversityMode, config.GetAntennaMode()); // Reusing SetAntennaMode since both GPIO_PIN_ANTENNA_SELECT and GPIO_PIN_NSS_2 will not be defined together.
  }

#if defined(POWER_OUTPUT_VALUES)
  // The last item (for MatchTX) will be MaxPower - MinPower + 1
  uint8_t luaPwrVal = (config.GetPower() == PWR_MATCH_TX) ? POWERMGNT::getMaxPower() + 1 : config.GetPower();
  setLuaTextSelectionValue(&luaTlmPower, luaPwrVal - POWERMGNT::getMinPower());
#endif

#if defined(GPIO_PIN_PWM_OUTPUTS)
  if (OPT_HAS_SERVO_OUTPUT)
  {
    const rx_config_pwm_t *pwmCh = config.GetPwmChannel(luaMappingChannelOut.properties.u.value - 1);
    setLuaUint8Value(&luaMappingChannelIn, pwmCh->val.inputChannel + 1);
    setLuaTextSelectionValue(&luaMappingOutputMode, pwmCh->val.mode);
    setLuaTextSelectionValue(&luaMappingInverted, pwmCh->val.inverted);
  }
#endif

  if (config.GetModelId() == 255)
  {
    setLuaStringValue(&luaModelNumber, "Off");
  }
  else
  {
    itoa(config.GetModelId(), modelString, 10);
    setLuaStringValue(&luaModelNumber, modelString);
  }
  setLuaTextSelectionValue(&luaVolatileBind, config.GetVolatileBind());
  return DURATION_IMMEDIATELY;
}

static int timeout()
{
  luaHandleUpdateParameter();
  // Receivers can only `UpdateParamReq == true` every 4th packet due to the transmitter cadence in 1:2
  // Channels, Downlink Telemetry Slot, Uplink Telemetry (the write command), Downlink Telemetry Slot...
  // (interval * 4 / 1000) or 1 second if not connected
  return (connectionState == connected) ? ExpressLRS_currAirRate_Modparams->interval / 250 : 1000;
}

static int start()
{
  registerLuaParameters();
  event();
  return DURATION_IMMEDIATELY;
}

device_t LUA_device = {
  .initialize = nullptr,
  .start = start,
  .event = event,
  .timeout = timeout
};

#endif
