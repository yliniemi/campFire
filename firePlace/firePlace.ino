#define NUM_LEDS_PER_STRIP 113
#define NUMSTRIPS 1
#define PALETTE_LENGTH 256
#define DEBUG_DELTA_SECONDS 10
#include "FastLED.h"
#include "I2SClocklessLedDriver.h"
#include "coordinates.h"

// #include "setupWifi.h"
#include "OTA.h"

// #include "myCredentials.h"        // oh yeah. there is myCredentials.zip on the root of this repository. include it as a library and then edit the file with your onw ips and stuff
// #include "settings.h"

#include "ESPAsyncWebSrv.h"
#include "ESPmDNS.h"


#include "AsyncWebConfig.h"

AsyncWebServer server(80);
AsyncWebConfig conf;

int32_t xy_scale = 29256;
int32_t z_scale = 5800;
int32_t z_max = z_scale * (NUM_LEDS_PER_STRIP - 1) * (NUM_LEDS_PER_STRIP - 1) / 256;
int64_t speed = 50;
uint8_t brightness = 255;
uint32_t milliwattLimit = 2000;
int32_t redDecline = 10000;
int32_t greenDecline = 1333;
int32_t blueDecline = 5000;
int64_t noiseFloor = 15000;
int64_t noiseScale = 64000;
char hostname[64] = "small_fire";
char otaPassword[64] = "huonosalasana";
int32_t otaRounds = 0;
char AP_SSID_PREFIX[64] = "fire-";
char AP_PSK[64] = "firesimulation";

int pins[]={12};
uint32_t coordinates[NUM_LEDS_PER_STRIP * 3];

String params = "["
  "{"
  "'name':'xy_scale',"
  "'label':'xy_scale',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'30000'"
  "},"
  "{"
  "'name':'z_scale',"
  "'label':'z_scale',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'5800'"
  "},"
  "{"
  "'name':'speed',"
  "'label':'speed',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'50'"
  "},"
  "{"
  "'name':'brightness',"
  "'label':'brightness',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':255,"
  "'default':'255'"
  "},"
  "{"
  "'name':'milliwattLimit',"
  "'label':'milliwattLimit',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':100000,"
  "'default':'2000'"
  "},"
  "{"
  "'name':'redDecline',"
  "'label':'redDecline',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'10000'"
  "},"
  "{"
  "'name':'greenDecline',"
  "'label':'greenDecline',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'1333'"
  "},"
  "{"
  "'name':'blueDecline',"
  "'label':'blueDecline',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'5000'"
  "},"
  "{"
  "'name':'noiseFloor',"
  "'label':'noiseFloor',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'15000'"
  "},"
  "{"
  "'name':'noiseScale',"
  "'label':'noiseScale',"
  "'type':"+String(INPUTNUMBER)+","
  "'min':0,'max':1000000000,"
  "'default':'64000'"
  "}"
  "]";

/*
struct CRGB {
  union {
    struct {
            union {
                uint8_t r;
                uint8_t red;
            };
            union {
                uint8_t g;
                uint8_t green;
            };
            union {
                uint8_t b;
                uint8_t blue;
            };
        };
    uint8_t raw[3];
  };
};
*/

CRGB palette[PALETTE_LENGTH];
CRGB leds[NUMSTRIPS * NUM_LEDS_PER_STRIP];

I2SClocklessLedDriver driver;

void generatePalette(CRGB *palette, int length)
{
    for (int i = 0; i < length; i++)
    {
        palette[i].r = max(min(255, (255 * redDecline - (255 * redDecline * i / (length - 1))) / 1000), 0);
        palette[i].g = max(255 - (255 * i * greenDecline / 1000 / length), 0);
        palette[i].b = max(255 - (255 * i * blueDecline / 1000 / length), 0);
    }
    z_max = z_scale * (NUM_LEDS_PER_STRIP - 1) * (NUM_LEDS_PER_STRIP - 1) / 256;
}

void changeCoordinates(uint32_t *coordinates)
{
    for (int i = 0; i < NUM_LEDS_PER_STRIP; i++)
    {
        coordinates[i * 3 + 0] = constantCoordinates[i * 3 + 0] * xy_scale / 256;
        coordinates[i * 3 + 1] = constantCoordinates[i * 3 + 1] * xy_scale / 256;
        coordinates[i * 3 + 2] = i * i * z_scale / 256;
    }
}

void updateAll()
{
    changeCoordinates(coordinates);
    generatePalette(palette, PALETTE_LENGTH);
    driver.setBrightness(brightness);
}

void createWifiAP()
{
    WiFi.mode(WIFI_AP);
    String apSsid = String(AP_SSID_PREFIX);
    apSsid.concat(WiFi.macAddress());
    apSsid.replace(":", "");
    WiFi.softAP(apSsid.c_str(), AP_PSK, 1);
    Serial.print("IP-Adresse = ");
    Serial.println(WiFi.localIP());
    Serial.print("Access Point = ");
    Serial.println(apSsid);
}

void handleRoot(AsyncWebServerRequest *request) {
  conf.handleFormRequest(request);
  if (request->hasParam("SAVE")) {
    uint8_t cnt = conf.getCount();
    Serial.println("*********** Configuration ************");
    for (uint8_t i = 0; i<cnt; i++) {
      Serial.print(conf.getName(i));
      Serial.print(" = ");
      Serial.println(conf.values[i]);
    }
  }
}

uint8_t limitCurrent(uint32_t milliwattLimit)
{
    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    for (int i = 0; i < NUM_LEDS_PER_STRIP; i++)
    {
        red += leds[i].r;
        green += leds[i].g;
        blue += leds[i].b;
    }
    uint32_t milliwatts = (red + green + blue) * driver._brightness / (25 * 255);
    if (milliwatts <= milliwattLimit) return 255;
    uint32_t scaleBrightness = 255 * milliwattLimit / milliwatts;
    for (int i = 0; i < NUM_LEDS_PER_STRIP * 3; i++)
    {
        ((uint8_t*)leds)[i] = (uint32_t)((uint8_t*)leds)[i] * scaleBrightness / 255;
    }
    return scaleBrightness;
}

void getSettings()
{
    xy_scale = conf.getInt("xy_scale");
    z_scale = conf.getInt("z_scale");
    z_max = z_scale * (NUM_LEDS_PER_STRIP - 1) * (NUM_LEDS_PER_STRIP - 1) / 256;
    speed = conf.getInt("speed");
    brightness = conf.getInt("brightness");
    milliwattLimit = conf.getInt("milliwattLimit");
    redDecline = conf.getInt("redDecline");
    greenDecline = conf.getInt("greenDecline");
    blueDecline = conf.getInt("blueDecline");
    noiseFloor = conf.getInt("noiseFloor");
    noiseScale = conf.getInt("noiseScale");
}

void setup() {
    Serial.begin(115200);
    printf("Started the fire place simulator\n");
    conf.setDescription(params);
    conf.readConfig();
    getSettings();
    updateAll();
    driver.initled((uint8_t*)leds, pins, NUMSTRIPS, NUM_LEDS_PER_STRIP, ORDER_GRB);
    createWifiAP();
    setupOTA(hostname, otaPassword, otaRounds);

    server.on("/",handleRoot);
    server.begin();

}

void loop()
{
    static uint32_t x_offset = esp_random();
    static uint32_t y_offset = esp_random();
    static uint32_t z_offset = esp_random();
    static uint32_t frameNumber = 0;
    static int64_t lastDebugTime = 0;
    int64_t time = esp_timer_get_time();

    frameNumber++;
    uint32_t z_location = (int64_t)time / (int64_t)256 * speed;
    for(int i = 0; i < NUM_LEDS_PER_STRIP; i++)
    {
        // leds[i] = palette[(uint8_t)(i - frameNumber / 4)];
        // leds[i] = palette[(i + 256 - NUM_LEDS_PER_STRIP)];
        int64_t noise = inoise16(coordinates[i * 3 + 0] + x_offset, coordinates[i * 3 + 1] + y_offset, coordinates[i * 3 + 2] + z_offset - z_location);
        noise = noise - noiseFloor + (int64_t)coordinates[i * 3 + 2] * noiseScale / (int64_t)z_max;
        leds[i] = palette[min(max((int32_t)noise / 256, 0), 255)];
    }

    limitCurrent(milliwattLimit);
    driver.showPixels((uint8_t*)leds);
    
    if (time > lastDebugTime + DEBUG_DELTA_SECONDS * 1000000)
    {
        static uint32_t debugFrameNumber = 0;
        printf("fps = %f\n", (float)(frameNumber - debugFrameNumber) / (float)DEBUG_DELTA_SECONDS);
        lastDebugTime += DEBUG_DELTA_SECONDS * 1000000;
        debugFrameNumber = frameNumber;
    }
    ArduinoOTA.handle();
}
