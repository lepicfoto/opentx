#include "../opentx.h"

TelemetryItem telemetryItems[TELEM_VALUES_MAX];

void TelemetryItem::gpsReceived()
{
  if (!distFromEarthAxis) {
    gps.extractLatitudeLongitude(&pilotLatitude, &pilotLongitude);
    uint32_t lat = pilotLatitude / 10000;
    uint32_t angle2 = (lat*lat) / 10000;
    uint32_t angle4 = angle2 * angle2;
    distFromEarthAxis = 139*(((uint32_t)10000000-((angle2*(uint32_t)123370)/81)+(angle4/25))/12500);
  }
  lastReceived = now();
}

void TelemetryItem::setValue(const TelemetrySensor & sensor, int32_t newVal, uint32_t unit, uint32_t prec)
{
  if (unit == UNIT_CELLS) {
    uint32_t data = uint32_t(newVal);
    uint8_t cellIndex = data & 0xF;
    uint8_t count = (data & 0xF0) >> 4;
    if (count != cells.count) {
      clear();
      cells.count = count;
    }
    cells.values[cellIndex].set(((data & 0x000FFF00) >>  8) / 5);
    if (cellIndex+1 < cells.count) {
      cells.values[cellIndex+1].set(((data & 0xFFF00000) >> 20) / 5);
    }
    if (cellIndex+2 >= cells.count) {
      newVal = 0;
      for (int i=0; i<count; i++) {
        if (cells.values[i].state) {
          newVal += cells.values[i].value;
        }
        else {
          // we didn't receive all cells values
          return;
        }
      }
      newVal = sensor.getValue(newVal, UNIT_VOLTS, 2);
    }
    else {
      // we didn't receive all cells values
      return;
    }
  }
  else if (unit == UNIT_DATETIME) {
    uint32_t data = uint32_t(newVal);
    if (data & 0x000000ff) {
      datetime.year = (uint16_t) ((data & 0xff000000) >> 24);
      datetime.month = (uint8_t) ((data & 0x00ff0000) >> 16);
      datetime.day = (uint8_t) ((data & 0x0000ff00) >> 8);
      datetime.datestate = 1;
      if (g_eeGeneral.adjustRTC) {
        struct gtm t;
        gettime(&t);
        t.tm_year = datetime.year-1900;
        t.tm_mon = datetime.month;
        t.tm_mday = datetime.day;
        rtcSetTime(&t);
      }
    }
    else {
      datetime.hour = ((uint8_t) ((data & 0xff000000) >> 24) + g_eeGeneral.timezone + 24) % 24;
      datetime.min = (uint8_t) ((data & 0x00ff0000) >> 16);
      datetime.sec = (uint16_t) ((data & 0x0000ff00) >> 8);
      datetime.timestate = 1;
      if (g_eeGeneral.adjustRTC) {
        struct gtm t;
        gettime(&t);
        if (abs((t.tm_hour-datetime.hour)*3600 + (t.tm_min-datetime.min)*60 + (t.tm_sec-datetime.sec)) > 20) {
          // we adjust RTC only if difference is > 20 seconds
          t.tm_hour = datetime.hour;
          t.tm_min = datetime.min;
          t.tm_sec = datetime.sec;
          g_rtcTime = gmktime(&t); // update local timestamp and get wday calculated
          rtcSetTime(&t);
        }
      }
    }
    if (datetime.year == 0) {
      return;
    }
    newVal = 0;
  }
  else if (unit == UNIT_GPS) {
    uint32_t gps_long_lati_data = uint32_t(newVal);
    uint32_t gps_long_lati_b1w, gps_long_lati_a1w;
    gps_long_lati_b1w = (gps_long_lati_data & 0x3fffffff) / 10000;
    gps_long_lati_a1w = (gps_long_lati_data & 0x3fffffff) % 10000;
    switch ((gps_long_lati_data & 0xc0000000) >> 30) {
      case 0:
        gps.latitude_bp = (gps_long_lati_b1w / 60 * 100) + (gps_long_lati_b1w % 60);
        gps.latitude_ap = gps_long_lati_a1w;
        gps.latitudeNS = 'N';
        break;
      case 1:
        gps.latitude_bp = (gps_long_lati_b1w / 60 * 100) + (gps_long_lati_b1w % 60);
        gps.latitude_ap = gps_long_lati_a1w;
        gps.latitudeNS = 'S';
        break;
      case 2:
        gps.longitude_bp = (gps_long_lati_b1w / 60 * 100) + (gps_long_lati_b1w % 60);
        gps.longitude_ap = gps_long_lati_a1w;
        gps.longitudeEW = 'E';
        break;
      case 3:
        gps.longitude_bp = (gps_long_lati_b1w / 60 * 100) + (gps_long_lati_b1w % 60);
        gps.longitude_ap = gps_long_lati_a1w;
        gps.longitudeEW = 'W';
        break;
    }
    if (gps.longitudeEW && gps.latitudeNS) {
      gpsReceived();
    }
    return;
  }
  else if (unit >= UNIT_GPS_LONGITUDE && unit <= UNIT_GPS_LATITUDE_NS) {
    uint32_t data = uint32_t(newVal);
    switch (unit) {
      case UNIT_GPS_LONGITUDE:
        gps.longitude_bp = data >> 16;
        gps.longitude_ap = data & 0xFFFF;
        break;
      case UNIT_GPS_LATITUDE:
        gps.latitude_bp = data >> 16;
        gps.latitude_ap = data & 0xFFFF;
        break;
      case UNIT_GPS_LONGITUDE_EW:
        gps.longitudeEW = data;
        break;
      case UNIT_GPS_LATITUDE_NS:
        gps.latitudeNS = data;
        break;
    }
    if (gps.longitudeEW && gps.latitudeNS && gps.longitude_ap && gps.latitude_ap) {
      gpsReceived();
    }
    return;
  }
  else if (unit == UNIT_DATETIME_YEAR) {
    datetime.year = newVal;
    return;
  }
  else if (unit == UNIT_DATETIME_DAY_MONTH) {
    uint32_t data = uint32_t(newVal);
    datetime.month = data >> 8;
    datetime.day = data & 0xFF;
    datetime.datestate = 1;
    return;
  }
  else if (unit == UNIT_DATETIME_HOUR_MIN) {
    uint32_t data = uint32_t(newVal);
    datetime.hour = ((data & 0xFF) + g_eeGeneral.timezone + 24) % 24;
    datetime.min = data >> 8;
  }
  else if (unit == UNIT_DATETIME_SEC) {
    datetime.sec = newVal & 0xFF;
    datetime.timestate = 1;
    newVal = 0;
  }
  else {
    newVal = sensor.getValue(newVal, unit, prec);
    if (sensor.inputFlags == TELEM_INPUT_FLAGS_AUTO_OFFSET) {
      if (!isAvailable()) {
        offsetAuto = -newVal;
      }
      newVal += offsetAuto;
    }
    else if (sensor.inputFlags == TELEM_INPUT_FLAGS_FILTERING) {
      if (!isAvailable()) {
        for (int i=0; i<TELEMETRY_AVERAGE_COUNT; i++) {
          filterValues[i] = newVal;
        }
      }
      else {
        // calculate the average from values[] and value
        // also shift readings in values [] array
        unsigned int sum = filterValues[0];
        for (int i=0; i<TELEMETRY_AVERAGE_COUNT-1; i++) {
          int32_t tmp = filterValues[i+1];
          filterValues[i] = tmp;
          sum += tmp;
        }
        filterValues[TELEMETRY_AVERAGE_COUNT-1] = newVal;
        sum += newVal;
        newVal = sum/(TELEMETRY_AVERAGE_COUNT+1);
      }
    }
  }

  if (!isAvailable()) {
    valueMin = newVal;
    valueMax = newVal;
  }
  else if (newVal < valueMin) {
    valueMin = newVal;
  }
  else if (newVal > valueMax) {
    valueMax = newVal;
    if (sensor.unit == UNIT_VOLTS) {
      valueMin = newVal;
    }
  }

  value = newVal;
  lastReceived = now();
}

bool TelemetryItem::isAvailable()
{
  return (lastReceived != TELEMETRY_VALUE_UNAVAILABLE);
}

bool TelemetryItem::isFresh()
{
  return (lastReceived < TELEMETRY_VALUE_TIMER_CYCLE) && (uint8_t(now() - lastReceived) < 2);
}

bool TelemetryItem::isOld()
{
  return (lastReceived == TELEMETRY_VALUE_OLD);
}

void TelemetryItem::per10ms(const TelemetrySensor & sensor)
{
  switch (sensor.formula) {
    case TELEM_FORMULA_CONSUMPTION:
      if (sensor.consumption.source) {
        TelemetrySensor & currentSensor = g_model.telemetrySensors[sensor.consumption.source-1];
        TelemetryItem & currentItem = telemetryItems[sensor.consumption.source-1];
        if (!currentItem.isAvailable()) {
          return;
        }
        else if (currentItem.isOld()) {
          lastReceived = TELEMETRY_VALUE_OLD;
          return;
        }
        int32_t current = convertTelemetryValue(currentItem.value, currentSensor.unit, currentSensor.prec, UNIT_AMPS, 1);
        currentItem.consumption.prescale += current;
        if (currentItem.consumption.prescale >= 3600) {
          currentItem.consumption.prescale -= 3600;
          setValue(sensor, value+1, sensor.unit, sensor.prec);
        }
      }
      break;

    default:
      break;
  }
}

void TelemetryItem::eval(const TelemetrySensor & sensor)
{
  switch (sensor.formula) {
    case TELEM_FORMULA_CELL:
      if (sensor.cell.source) {
        TelemetryItem & cellsItem = telemetryItems[sensor.cell.source-1];
        if (cellsItem.isOld()) {
          lastReceived = TELEMETRY_VALUE_OLD;
        }
        else {
          unsigned int index = sensor.cell.index;
          if (index == TELEM_CELL_INDEX_LOWEST || index == TELEM_CELL_INDEX_HIGHEST || index == TELEM_CELL_INDEX_DELTA) {
            unsigned int lowest=0, highest=0;
            for (int i=0; i<cellsItem.cells.count; i++) {
              if (cellsItem.cells.values[i].state) {
                if (!lowest || cellsItem.cells.values[i].value < cellsItem.cells.values[lowest-1].value)
                  lowest = i+1;
                if (!highest || cellsItem.cells.values[i].value > cellsItem.cells.values[highest-1].value)
                  highest = i+1;
              }
              else {
                lowest = highest = 0;
              }
            }
            if (lowest) {
              switch (index) {
                case TELEM_CELL_INDEX_LOWEST:
                  setValue(sensor, cellsItem.cells.values[lowest-1].value, UNIT_VOLTS, 2);
                  break;
                case TELEM_CELL_INDEX_HIGHEST:
                  setValue(sensor, cellsItem.cells.values[highest-1].value, UNIT_VOLTS, 2);
                  break;
                case TELEM_CELL_INDEX_DELTA:
                  setValue(sensor, cellsItem.cells.values[highest-1].value - cellsItem.cells.values[lowest-1].value, UNIT_VOLTS, 2);
                  break;
              }
            }
          }
          else {
            index -= 1;
            if (index < cellsItem.cells.count && cellsItem.cells.values[index].state) {
              setValue(sensor, cellsItem.cells.values[index].value, UNIT_VOLTS, 2);
            }
          }
        }
      }
      break;

    case TELEM_FORMULA_DIST:
      if (sensor.dist.gps) {
        TelemetryItem gpsItem = telemetryItems[sensor.dist.gps-1];
        TelemetryItem * altItem = NULL;
        if (!gpsItem.isAvailable()) {
          return;
        }
        else if (gpsItem.isOld()) {
          lastReceived = TELEMETRY_VALUE_OLD;
          return;
        }
        if (sensor.dist.alt) {
          altItem = &telemetryItems[sensor.dist.alt-1];
          if (!altItem->isAvailable()) {
            return;
          }
          else if (altItem->isOld()) {
            lastReceived = TELEMETRY_VALUE_OLD;
            return;
          }
        }
        uint32_t latitude, longitude;
        gpsItem.gps.extractLatitudeLongitude(&latitude, &longitude);

        uint32_t angle = (latitude > gpsItem.pilotLatitude) ? latitude - gpsItem.pilotLatitude : gpsItem.pilotLatitude - latitude;
        uint32_t dist = EARTH_RADIUS * angle / 1000000;
        uint32_t result = dist*dist;

        angle = (longitude > gpsItem.pilotLongitude) ? longitude - gpsItem.pilotLongitude : gpsItem.pilotLongitude - longitude;
        dist = gpsItem.distFromEarthAxis * angle / 1000000;
        result += dist*dist;

        if (altItem) {
          dist = abs(altItem->value);
          uint8_t prec = g_model.telemetrySensors[sensor.dist.alt-1].prec;
          if (prec > 0)
            dist /= (prec==2 ? 100 : 10);
          result += dist*dist;
        }

        setValue(sensor, isqrt32(result), UNIT_METERS);
      }
      break;

    case TELEM_FORMULA_ADD:
    case TELEM_FORMULA_AVERAGE:
    case TELEM_FORMULA_MIN:
    case TELEM_FORMULA_MAX:
    case TELEM_FORMULA_MULTIPLY:
    {
      int32_t value=0, count=0, available=0, maxitems=4, mulprec=0;
      if (sensor.formula == TELEM_FORMULA_MULTIPLY) {
        maxitems = 2;
        value = 1;
      }
      for (int i=0; i<maxitems; i++) {
        int8_t source = sensor.calc.sources[i];
        if (source) {
          unsigned int index = abs(source)-1;
          TelemetrySensor & telemetrySensor = g_model.telemetrySensors[index];
          TelemetryItem & telemetryItem = telemetryItems[index];
          if (sensor.formula == TELEM_FORMULA_AVERAGE) {
            if (telemetryItem.isAvailable())
              available = 1;
            else
              continue;
            if (telemetryItem.isOld())
              continue;
          }
          else {
            if (!telemetryItem.isAvailable()) {
              return;
            }
            else if (telemetryItem.isOld()) {
              lastReceived = TELEMETRY_VALUE_OLD;
              return;
            }
          }
          int32_t sensorValue = telemetryItem.value;
          if (source < 0)
            sensorValue = -sensorValue;
          count += 1;
          if (sensor.formula == TELEM_FORMULA_MULTIPLY) {
            mulprec += telemetrySensor.prec;
            value *= convertTelemetryValue(sensorValue, telemetrySensor.unit, 0, sensor.unit, 0);
          }
          else {
            sensorValue = convertTelemetryValue(sensorValue, telemetrySensor.unit, telemetrySensor.prec, sensor.unit, sensor.prec);
            if (sensor.formula == TELEM_FORMULA_MIN)
              value = (count==1 ? sensorValue : min<int32_t>(value, sensorValue));
            else if (sensor.formula == TELEM_FORMULA_MAX)
              value = (count==1 ? sensorValue : max<int32_t>(value, sensorValue));
            else
              value += sensorValue;
          }
        }
      }
      if (sensor.formula == TELEM_FORMULA_AVERAGE) {
        if (count == 0) {
          if (available)
            lastReceived = TELEMETRY_VALUE_OLD;
          return;
        }
        else {
          value = (value + count/2) / count;
        }
      }
      else if (sensor.formula == TELEM_FORMULA_MULTIPLY) {
        if (count == 0)
          return;
        value = convertTelemetryValue(value, sensor.unit, mulprec, sensor.unit, sensor.prec);
      }
      setValue(sensor, value, sensor.unit, sensor.prec);
      break;
    }

    default:
      break;
  }
}

int getTelemetryIndex(TelemetryProtocol protocol, uint16_t id, uint8_t instance)
{
  int available = -1;

  for (int index=0; index<TELEM_VALUES_MAX; index++) {
    TelemetrySensor & telemetrySensor = g_model.telemetrySensors[index];
    if (telemetrySensor.id == id && telemetrySensor.instance == instance) {
      return index;
    }
    else if (available < 0 && telemetrySensor.id == 0) {
      available = index;
    }
  }

  if (available >= 0) {
    switch (protocol) {
#if defined(FRSKY_SPORT)
      case TELEM_PROTO_FRSKY_SPORT:
        frskySportSetDefault(available, id, instance);
        break;
#endif
#if defined(FRSKY)
      case TELEM_PROTO_FRSKY_D:
        frskyDSetDefault(available, id);
        break;
#endif
      default:
        break;
    }
  }

  return available;
}

void delTelemetryIndex(uint8_t index)
{
  memclear(&g_model.telemetrySensors[index], sizeof(TelemetrySensor));
  telemetryItems[index].clear();
  eeDirty(EE_MODEL);
}

int availableTelemetryIndex()
{
  for (int index=0; index<TELEM_VALUES_MAX; index++) {
    TelemetrySensor & telemetrySensor = g_model.telemetrySensors[index];
    if (!telemetrySensor.isAvailable()) {
      return index;
    }
  }
  return -1;
}

void setTelemetryValue(TelemetryProtocol protocol, uint16_t id, uint8_t instance, int32_t value, uint32_t unit, uint32_t prec)
{
  int index = getTelemetryIndex(protocol, id, instance);

  if (index >= 0) {
    telemetryItems[index].setValue(g_model.telemetrySensors[index], value, unit, prec);
  }
  else {
    // TODO error too many sensors
  }
}

void TelemetrySensor::init(const char *label, uint8_t unit, uint8_t prec)
{
  memclear(this->label, TELEM_LABEL_LEN);
  strncpy(this->label, label, TELEM_LABEL_LEN);
  this->unit = unit;
  this->prec = prec;
  // this->inputFlags = inputFlags;
}

void TelemetrySensor::init(uint16_t id)
{
  char label[4];
  label[0] = hex2zchar((id & 0xf000) >> 12);
  label[1] = hex2zchar((id & 0x0f00) >> 8);
  label[2] = hex2zchar((id & 0x00f0) >> 4);
  label[3] = hex2zchar((id & 0x000f) >> 0);
  init(label);
}

bool TelemetrySensor::isAvailable()
{
  return ZLEN(label) > 0;
}

int32_t convertTelemetryValue(int32_t value, uint8_t unit, uint8_t prec, uint8_t destUnit, uint8_t destPrec)
{
  for (int i=prec; i<destPrec; i++)
    value *= 10;

  if (unit == UNIT_METERS) {
    if (destUnit == UNIT_FEET) {
      // m to ft *105/32
      value = (value * 105) / 32;
    }
  }
  else if (unit == UNIT_KTS) {
    if (destUnit == UNIT_KMH) {
      // kts to km/h
      value = (value * 1852) / 1000;
    }
    else if (destUnit == UNIT_MPH) {
      // kts to mph
      value = (value * 23) / 20;
    }
  }
  else if (unit == UNIT_CELSIUS) {
    if (destUnit == UNIT_FAHRENHEIT) {
      // T(°F) = T(°C)×1,8 + 32
      value = 32 + (value*18)/10;
    }
  }

  for (int i=destPrec; i<prec; i++)
    value /= 10;

  return value;
}

int32_t TelemetrySensor::getValue(int32_t value, uint8_t unit, uint8_t prec) const
{
  if (type == TELEM_TYPE_CUSTOM && custom.ratio) {
    if (this->prec == 2) {
      value *= 10;
      prec = 2;
    }
    else {
      prec = 1;
    }
    value = (custom.ratio * value + 122) / 255;
  }

  value = convertTelemetryValue(value, unit, prec, this->unit, this->prec);

  if (type == TELEM_TYPE_CUSTOM) {
    value += custom.offset;
  }

  return value;
}

