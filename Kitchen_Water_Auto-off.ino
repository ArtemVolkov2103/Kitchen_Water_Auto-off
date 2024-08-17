#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "Hash.h"
#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"
const char* ssid = "ESP8266";
const char* password = "password";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events");
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <h1>
    ESP8266 Kitchen
  </h1>
  <h3>
    Water volume
  </h3>
  <span class="reading"><span id="volume">
    %VOLUME%
  </span> L</span>
  <h3>
    Waterflow speed
  </h3>
  <span class="reading"><span id="speed">
    %SPEED%
  </span> L/minute</span>
  <h3>
    Total water spent:
  </h3>
  <span class="reading"><span id="totalvolume">
    %TOTALVOLUME%
  </span> L</span>
  <script>
if (!!window.EventSource) {
var source = new EventSource('/events');
source.addEventListener('open', function(e) {
console.log("Events Connected");
}, false);
source.addEventListener('error', function(e) {
if (e.target.readyState != EventSource.OPEN) {
console.log("Events Disconnected");
}
}, false);
source.addEventListener('message', function(e) {
console.log("message", e.data);
}, false);
source.addEventListener('volume', function(e) {
console.log("volume", e.data);
document.getElementById("volume").innerHTML = e.data;
}, false);
source.addEventListener('speed', function(e) {
console.log("speed", e.data);
document.getElementById("speed").innerHTML = e.data;
}, false);
source.addEventListener('totalvolume', function(e) {
console.log("totalvolume", e.data);
document.getElementById("totalvolume").innerHTML = e.data;
}, false);
}
</script>
</body>
</html>

)rawliteral";

long previousMillis = 0;   // храним время последнего переключения светодиода
long interval = 100;       // интервал между отправкой новых значений по Wi-Fi (0.1 секунда)
long previousMillisW = 0;  // храним время последней проверки обьёма
long intervalW = 5000;     // интервал между проверками, открыт ли кран
float lastVol = 0;
uint8_t pinSensor = D0;  // Определяем номер вывода Arduino, к которому подключён датчик расхода воды.
                         //
float varQ;              // Объявляем переменную для хранения рассчитанной скорости потока воды (л/с).
float varV;              // Объявляем переменную для хранения рассчитанного объема воды (л).
uint32_t varL;
int triggered = 0;
float waterTotal = 0;     //общий объем воды, который потратили
// Заменяет заглушки реальными значенияим
String processor(const String& var) {
  //Serial.println(var);
  if (var == "VOLUME") {
    return String(varV);
  } else if (var == "SPEED") {
    return String(varQ);
  }else if (var == "TOTALVOLUME") {
    return String(waterTotal);
  }
  return String();
}

//
void setup() {                //
  Serial.begin(9600);         // Инициируем передачу данных в монитор последовательного порта.
  pinMode(pinSensor, INPUT);  // Конфигурируем вывод к которому подключён датчик, как вход.
  pinMode(D5, OUTPUT);
  varQ = 0;
  varV = 0;                   // Обнуляем все переменные.
  digitalWrite(D5, LOW);

  Serial.print("Setting AP (Access Point)…");
  // Remove the password parameter, if you want the AP (Access Point) to be open
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Print ESP8266 Local IP Address
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/volume", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/plain", String(varV).c_str());
  });
  server.on("/speed", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/plain", String(varQ).c_str());
  });
  server.on("/totalvolume", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/plain", String(waterTotal).c_str());
  });
  server.addHandler(&events);
  // Start server
  server.begin();
}

void loop() {
  calculateVolume();
  Serial.println((String) "Объем " + varV + "л, скорость " + (varQ * 60.0f) + "л/м.");
  unsigned long currentMillis = millis();
  //проверяем не прошел ли нужный интервал, если прошел то
  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    events.send(String(varV).c_str(), "volume", millis());
    events.send(String(varQ).c_str(), "speed", millis());
    events.send(String(waterTotal).c_str(), "totalvolume", millis());
  }
  if (currentMillis - previousMillisW > intervalW) { //через заданное время проверяем, включен ли кран
    previousMillisW = currentMillis;
    if (varQ == 0 && varV < 1.5) {            //если кран закрыт и 1.5 литра еще не набрано
      if (lastVol == varV)                    //если кран закрыт уже intervalW времени (значения между измерениями не поменялись)
        varV = 0;                             //обнуляем счетчик налитой воды
      else lastVol = varV;                    //приравниваем значения для того, чтобы сравнить в следующий раз
    }
  }
  if (varV > 1.5) {                           //если чайник набран
    digitalWrite(D5, HIGH);                   //включаю клапан, перекрывающий воду
    delay(1000);
    digitalWrite(D5, LOW);                    //выключаю клапан, перекрывающий воду, ударившая струя воды сигнализирует о том, что нужно закрыть кран
    delay(500);
    calculateVolume();

    if (varQ > 0) {                           //если кран открыт
      digitalWrite(D5, HIGH);                 //выключаю клапан, ударившая струя воды сигнализирует о том, что нужно закрыть кран
      triggered++;                            //счетчик для срабатываний клапана. 
      Serial.println(triggered);

      if (triggered >= 3) {                   //клапан отработал 4 раза, кран не закрыт, надо перекрывать воду
        digitalWrite(D5, HIGH);               //если программа дошла до этого момента, то кто-то забыл/не успевает выключить кран
        Serial.println("wait 1 minute");
        delay(60000);                         //1 минуту жду, пока не закроют кран. Если кран будет открыт, то еще минуту ждать буду
        calculateVolume();
      }
    }

    else {
      varV = 0;
      triggered = 0;
      digitalWrite(D5, LOW);
    }
  }
}

void calculateVolume() {
  varQ = 0;                                    // Сбрасываем скорость потока воды.
  varL = pulseIn(pinSensor, HIGH, 200000);     // Считываем длительность импульса, но не дольше 0,2 сек.
  if (varL) {                                  // Если длительность импульса считана, то ...
    float varT = 2.0 * (float)varL / 1000000;  // Определяем период следования импульсов в сек.
    float varF = 1 / varT;                     // Определяем частоту следования импульсов в Гц.
    varQ = varF / 680.0f;                      //450.0f;//(varF * 5.9f + 4570.0f);           // Определяем скорость потока воды л/c.
    varV += varQ * varT;                       // Определяем объем воды л.
    waterTotal += varQ * varT;                 // Записываем, сколько воды потрачено
    varQ = varQ * 60;                          // переводим в л/м
  }
}