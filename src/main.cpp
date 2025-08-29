#include <M5Cardputer.h>
#include <SPIFFS.h>

#include <string>

#include "BLE2902.h"
#include "BLEAdvertisedDevice.h"
#include "BLEClient.h"
#include "BLEDevice.h"
#include "BLEScan.h"
#include "BLEServer.h"
#include "BLEUtils.h"

const char* SoundFile[] = {
    "/cat.wav", "/dog.wav", "/cow.wav", "/sheep.wav", "/rooster.wav",
};

struct NotificationSound_T {
  const float freq;
  const int duration;
};

NotificationSound_T const NotificationSound[] = {
    {2000, 5},   {1500, 5},   {2500, 5},   {2000, 100}, {1500, 100},
    {2500, 100}, {2000, 200}, {1500, 200}, {2500, 200}};

// 配列サイズを定数として定義
constexpr size_t SOUND_FILE_COUNT = sizeof(SoundFile) / sizeof(SoundFile[0]);
constexpr size_t NOTIFICATION_SOUND_COUNT =
    sizeof(NotificationSound) / sizeof(NotificationSound[0]);

// UUIDs
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// グローバル変数
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
BLEScan* pBLEScan = nullptr;
std::vector<BLEClient*> connectedClients;
bool deviceConnected = false;
int deviceNumber = random(1500, 2500);
String deviceName = "_SC_" + String(deviceNumber);

BLEAdvertisedDevice nextTargetDevice;
bool doConnect = false;

// 異常終了したときにESP32をリスタートします。
void fail() {
  // リスタートに失敗したとしてもリスタートを繰り返す
  while (1) {
    ESP.restart();
  }
}

void playNotification(int type) {
  if (type < 0 || type >= NOTIFICATION_SOUND_COUNT) {
    return;
  }
  const NotificationSound_T sound = NotificationSound[type];
  M5Cardputer.Speaker.tone(sound.freq, sound.duration);
}

void playDeviceNumber() { M5Cardputer.Speaker.tone(float(deviceNumber), 100); }
void playFile(const char* fileName) {
  if (!SPIFFS.exists(fileName)) {
    fail();
  }
  File wavFile = SPIFFS.open(fileName, "r");
  if (!wavFile) {
    fail();
  }

  size_t fileSize = wavFile.size();
  uint8_t* wavData = new uint8_t[fileSize];
  if (!wavData) {
    fail();
  }

  wavFile.read(wavData, fileSize);
  wavFile.close();
  M5Cardputer.Speaker.playWav(wavData, fileSize);
  delete[] wavData;
}

void processMessage(std::string message) {
  String body = message.substr(5).c_str();
  // メッセージを解析して適切なアクションを実行
  if (body.startsWith("p:")) {
    String soundFile = body.substring(2);
    playFile(soundFile.c_str());
  } else if (body.startsWith("n:")) {
    int type = body.substring(7).toInt();
    playNotification(type);
  } else if (body.startsWith("d:")) {
    playDeviceNumber();
  }
}

// メッセージを全デバイスにブロードキャスト
void broadcastMessage(String message) {
  // サーバーとして接続された端末に送信
  if (deviceConnected && pCharacteristic != nullptr) {
    pCharacteristic->setValue(message.c_str());
    pCharacteristic->notify();
  }

  // クライアントとして接続された端末に送信
  for (BLEClient* client : connectedClients) {
    if (client->isConnected()) {
      BLERemoteService* pRemoteService = client->getService(SERVICE_UUID);
      if (pRemoteService != nullptr) {
        BLERemoteCharacteristic* pRemoteCharacteristic =
            pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
        if (pRemoteCharacteristic != nullptr) {
          pRemoteCharacteristic->writeValue(message.c_str());
        }
      }
    }
  }
}

void connectToDevice(BLEAdvertisedDevice device);

// サーバーコールバック
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("デバイスが接続されました");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("デバイスが切断されました");
    BLEDevice::startAdvertising();
  }
};

// 特性コールバック（メッセージ受信）
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.printf("received %s\n", rxValue.c_str());
      playNotification(1);
      M5Cardputer.Display.print(rxValue.c_str());
      // 他のすべてのクライアントに転送
      // broadcastMessage(message);
      processMessage(rxValue);
    }
  }
};

// クライアント接続コールバック
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("クライアントとして接続しました");
  }

  void onDisconnect(BLEClient* pclient) {
    Serial.println("クライアントとして切断されました");
    // 切断されたクライアントをリストから削除
    for (auto it = connectedClients.begin(); it != connectedClients.end();
         ++it) {
      if (*it == pclient) {
        connectedClients.erase(it);
        break;
      }
    }
  }
};

// 広告デバイス検出コールバック
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      Serial.println("対象デバイスを発見: " +
                     String(advertisedDevice.getName().c_str()));

      // 既に接続済みかチェック
      std::string deviceAddress =
          std::string(advertisedDevice.getAddress().toString().c_str());
      for (BLEClient* client : connectedClients) {
        if (client->getPeerAddress().toString() == deviceAddress) {
          return;  // 既に接続済み
        }
      }

      nextTargetDevice = advertisedDevice;
      doConnect = true;
    }
  }
};

// デバイスに接続
void connectToDevice(BLEAdvertisedDevice device) {
  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  if (pClient->connect(&device)) {
    Serial.println("接続成功");

    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService != nullptr) {
      BLERemoteCharacteristic* pRemoteCharacteristic =
          pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);

      if (pRemoteCharacteristic != nullptr) {
        connectedClients.push_back(pClient);
        Serial.println("サービス接続完了");
      }
    }
  } else {
    Serial.println("接続失敗");
    delete pClient;
  }
}

// BLEサーバー初期化
void initBLEServer() {
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE |
                               BLECharacteristic::PROPERTY_NOTIFY);

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("BLEサーバー開始、アドバタイジング中...");
}

// BLEスキャン初期化
void initBLEScan() {
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void keyClick(const m5::Keyboard_Class::KeysState& key) {
  if (key.enter)
    playNotification(1);
  else if (key.word.empty())
    playNotification(2);
  else
    playNotification(0);
}

void keyInput(m5::Keyboard_Class::KeysState& keys_status) {
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      keys_status = M5Cardputer.Keyboard.keysState();
      keyClick(keys_status);
    }
  }
}

String line;

void sendPlayCommand(const char* playFileName) {
  String message = String(deviceNumber) + ":p:" + playFileName;
  Serial.println("Send: " + message);
  broadcastMessage(message);
  line.clear();
}

void sendLine() {
  if (line.isEmpty()) return;
  String fullMessage = deviceName;
  fullMessage.replace("_SC_", "");
  fullMessage += ":c:" + line;
  Serial.println("Send: " + fullMessage);
  broadcastMessage(fullMessage);
  line.clear();
}

void keyProcess(const m5::Keyboard_Class::KeysState& keys_status) {
  if (keys_status.enter) {
    sendLine();
  }
  if (keys_status.word.empty()) return;

  char key = keys_status.word[0];
  // 音声ファイルを再生する特殊処理
  if (key >= '1' && key <= '9') {
    switch (key) {
      case '1':
        sendPlayCommand(SoundFile[0]);
        break;
      case '2':
        sendPlayCommand(SoundFile[1]);
        break;
      case '3':
        sendPlayCommand(SoundFile[2]);
        break;
      case '4':
        sendPlayCommand(SoundFile[3]);
        break;
      case '5':
        sendPlayCommand(SoundFile[4]);
        break;
      default:
        break;
    }
  } else
    line += key;
}

void ScanTask(void* parameter) {
  while (true) {
    // 定期的にスキャンを実行
    static unsigned long lastScan = 0;
    if (millis() - lastScan > 5000) {
      // playDeviceNumber();
      pBLEScan->start(1, nullptr, false);
      lastScan = millis();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void initialize() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Speaker.setVolume(128);

  // ディスプレイの初期化
  M5Cardputer.Display.begin();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.println("Hello!");

  // SPIFFSの初期化
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    fail();
  }

  Serial.println("BLE通信システム開始");
  BLEDevice::init(deviceName.c_str());
  initBLEServer();
  initBLEScan();
  Serial.println("デバイス名: " + deviceName);

  // ScanTaskを別タスクで実行
  xTaskCreatePinnedToCore(ScanTask,    // タスク関数
                          "ScanTask",  // タスク名
                          2048,        // スタックサイズ
                          NULL,        // 引数
                          1,           // 優先度
                          NULL,        // タスクハンドル
                          1            // コアID
  );
}

void setup() { initialize(); }

void loop() {
  M5Cardputer.update();

  if (doConnect) {
    // BLE接続処理
    doConnect = false;
    connectToDevice(nextTargetDevice);
  }

  // キーボードからの入力をチェック
  if (M5Cardputer.BtnA.wasPressed()) {
    playFile(SoundFile[0]);
  }
  m5::Keyboard_Class::KeysState keys_status;
  keyInput(keys_status);
  keyProcess(keys_status);
  delay(10);
}
