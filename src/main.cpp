#include <M5Cardputer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_now.h>

// 送信先のMACアドレス（実際の相手デバイスのMACアドレスに変更してください）
constexpr uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF};  // ブロードキャスト
// メッセージ構造体

constexpr uint8_t MESSAGE_SIZE = 241;
struct struct_message {
  int device_number;
  char command[3];
  char text[MESSAGE_SIZE];
};

struct_message incomingMessage;
struct_message outgoingMessage;

struct NotificationSound_T {
  const float freq;
  const int duration;
};

NotificationSound_T const NotificationSound[] = {
    {2000, 5},   {1500, 5},   {2500, 5},   {2000, 100}, {1500, 100},
    {2500, 100}, {2000, 200}, {1500, 200}, {2500, 200}};

const char *SoundFile[] = {
    "/bird.wav",   "/cat.wav",      "/cat2.wav",    "/correct.wav",
    "/cow.wav",    "/cricket.wav",  "/dog.wav",     "/elephant.wav",
    "/frog.wav",   "/horse.wav",    "/lion.wav",    "/minsemi.wav",
    "/monkey.wav", "/ra_snake.wav", "/rooster.wav", "/sheep.wav",
    "/start.wav",  "/take.wav",     "/wolf.wav",    "/wrong.wav"};

// 配列サイズを定数として定義
constexpr size_t SOUND_FILE_COUNT = sizeof(SoundFile) / sizeof(SoundFile[0]);
constexpr size_t NOTIFICATION_SOUND_COUNT =
    sizeof(NotificationSound) / sizeof(NotificationSound[0]);

int deviceNumber = random(1500, 2500);
String deviceName = "_SC_" + String(deviceNumber);

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

void playFile(const char *fileName) {
  if (!SPIFFS.exists(fileName)) {
    Serial.println("File does not exist!");
    return;
  }
  File wavFile = SPIFFS.open(fileName, "r");
  if (!wavFile) {
    Serial.println("Failed to open WAV file!");
    return;
  }

  size_t fileSize = wavFile.size();
  uint8_t *wavData = new uint8_t[fileSize];
  if (!wavData) {
    wavFile.close();
    Serial.println("Memory allocation failed!");
    return;
  }

  wavFile.read(wavData, fileSize);
  wavFile.close();
  M5Cardputer.Speaker.playWav(wavData, fileSize);
  delete[] wavData;
}

// ESP-NOW送信コールバック
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    playFile(SoundFile[3]);
    M5Cardputer.Display.println("Message send failed!");
  }
}

// ESP-NOW受信コールバック
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
  Serial.print("Received: ");
  Serial.println(incomingMessage.command);
  M5Cardputer.Display.println(incomingMessage.text);
  if (strcmp(incomingMessage.command, "p") == 0) {
    playFile(incomingMessage.text);
  }
}

bool broadcastMessage() {
  esp_err_t result = esp_now_send(broadcastMAC, (uint8_t *)&outgoingMessage,
                                  sizeof(outgoingMessage));
  if (result == ESP_OK) {
    Serial.println(outgoingMessage.text);
    return true;
  } else {
    Serial.println("Error sending message");
    M5Cardputer.Display.println("Send error!");
    return false;
  }
}

void keyClick(const m5::Keyboard_Class::KeysState &key) {
  if (key.enter)
    playNotification(1);
  else if (key.word.empty())
    playNotification(2);
  else
    playNotification(0);
}

void keyInput(m5::Keyboard_Class::KeysState &keys_status) {
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      keys_status = M5Cardputer.Keyboard.keysState();
      keyClick(keys_status);
    }
  }
}

String line;

void sendPlayCommand(const char *playFileName) {
  outgoingMessage.device_number = deviceNumber;
  outgoingMessage.command[0] = 'p';
  strncpy(outgoingMessage.text, playFileName, MESSAGE_SIZE);
  Serial.printf("Send: p:%s\n", outgoingMessage.text);
  broadcastMessage();
  line.clear();
}

void sendLine() {
  if (line.isEmpty()) return;
  outgoingMessage.device_number = deviceNumber;
  outgoingMessage.command[0] = 'c';
  strncpy(outgoingMessage.text, line.c_str(), MESSAGE_SIZE);
  Serial.printf("Send: c:%s\n", outgoingMessage.text);
  broadcastMessage();
  line.clear();
}

void keyProcess(const m5::Keyboard_Class::KeysState &keys_status) {
  if (keys_status.enter) {
    sendLine();
  }
  if (keys_status.word.empty()) return;

  char key = keys_status.word[0];
  // 音声ファイルを再生する特殊処理
  if (key >= '1' && key <= '9') {
    switch (key) {
      case '1':
        sendPlayCommand(SoundFile[5]);
        break;
      case '2':
        sendPlayCommand(SoundFile[4]);
        break;
      case '3':
        sendPlayCommand(SoundFile[7]);
        break;
      case '4':
        sendPlayCommand(SoundFile[8]);
        break;
      case '5':
        sendPlayCommand(SoundFile[9]);
        break;
      case '6':
        sendPlayCommand(SoundFile[10]);
        break;
      default:
        break;
    }
  } else
    line += key;
}

void initialize() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Speaker.setVolume(32);

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

  // WiFiをステーションモードに設定
  WiFi.mode(WIFI_STA);

  // ESP-NOWを初期化
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    M5Cardputer.Display.println("ESP-NOW init failed!");
    fail();
  }

  // 送信コールバックを登録
  esp_now_register_send_cb(OnDataSent);

  // 受信コールバックを登録
  esp_now_register_recv_cb(OnDataRecv);

  // ピア情報を設定
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  // ピアを追加
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    M5Cardputer.Display.println("Failed to add peer");
    return;
  }

  Serial.println("ESP-NOW initialized successfully");
  M5Cardputer.Display.println("Hello!");
  playFile("/correct.wav");
}

void setup() { initialize(); }

void loop() {
  M5Cardputer.update();
  // キーボードからの入力をチェック
  if (M5Cardputer.BtnA.wasPressed()) {
    playFile(SoundFile[0]);
  }
  m5::Keyboard_Class::KeysState keys_status;
  keyInput(keys_status);
  keyProcess(keys_status);
  delay(10);
}
