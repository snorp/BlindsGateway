#include <EEPROMex.h>
#include <Node.h>

#define LED_PIN 9

#define CMD_START 0x02
#define CMD_LENGTH 20
#define MAX_PAYLOAD_LENGTH 256
#define ENCRYPT_KEY "ak2950sjzcoajsd9"

enum CommandCode {
  CMD_ACK = 1,
  CMD_MESSAGE = 2,
  CMD_DEBUG = 3,
  CMD_PING = 4
};

enum ACKCode {
  ACK_OK = 0,
  ACK_INVALID = 1,
  ACK_FAIL = 2,
  ACK_MSG = 3
};

struct CommandHeader {
  uint8_t start; // should be CMD_START
  uint8_t code;
  uint8_t length;
};

Node node;

static void ledOn(void)
{
  digitalWrite(LED_PIN, HIGH);
}

static void ledOff(void)
{
  digitalWrite(LED_PIN, LOW);
}

static void writeUint16(uint16_t num)
{
  Serial.write(byte(num >> 8));
  Serial.write(byte(num & 0xff));
}

static void replyHeader(CommandCode code, uint8_t length)
{
  Serial.write(CMD_START);
  Serial.write(byte(code));
  Serial.write(length);
}

static void reply(ACKCode code)
{
  replyHeader(CMD_ACK, 1);
  Serial.write(byte(code));
}

static void reply(Message& msg)
{
  replyHeader(CMD_MESSAGE, sizeof(Message));

  Serial.write(msg.from);
  Serial.write(msg.to);
  writeUint16(msg.cmd.op);
  writeUint16(msg.cmd.arg1);
  writeUint16(msg.cmd.arg2);
  Serial.write(msg.cmd.group);
  Serial.write(msg.cmd.flags);
}

static bool readBlock(uint8_t* buffer, size_t len)
{
  if (Serial.available() < len) {
    // We don't block
    return false;
  }

  for (int i = 0; i < len; i++) {
    buffer[i] = Serial.read();
  }

  return true;
}

static uint16_t readUint16(void)
{
  return (uint16_t(Serial.read()) << 8) | uint16_t(Serial.read());
}

static bool canReadHeader()
{
  return Serial.available() >= sizeof(CommandHeader);
}

static void resetHeader(CommandHeader& header) {
  memset(&header, 0, sizeof(CommandHeader));
}

static bool readHeader(CommandHeader& header)
{
  if (!readBlock((uint8_t*)&header, sizeof(CommandHeader))) {
    return false;
  }

  if (header.start != CMD_START) {
    resetHeader(header);
    return false;
  }

  return true;
}

static void discardPayload(CommandHeader& header)
{
  for (int i = 0; i < header.length; i++) {
    Serial.read();
  }

  resetHeader(header);
}

#define FLASH_PAYLOAD_SIZE 55
static bool pushFlashImage(uint8_t to, uint16_t length)
{
  uint16_t bytesSent = 0;
  byte buf[FLASH_PAYLOAD_SIZE];
  long lastChunkMillis = millis();

  delay(100);

  while (bytesSent < length && (millis() - lastChunkMillis) < 3000) {
    uint16_t chunkSize = min(FLASH_PAYLOAD_SIZE, length - bytesSent);

    if (Serial.available() < chunkSize) {
      continue;
    }

    if (Serial.readBytes(buf, chunkSize) != chunkSize) {
      reply(ACK_FAIL);
      break;
    }

    if (!node.radio().sendWithRetry(to, buf, chunkSize, 20, 100)) {
      reply(ACK_FAIL);
      break;
    }

    bytesSent += chunkSize;
    lastChunkMillis = millis();
    reply(ACK_OK);
  }

  return bytesSent == length;
}

static bool readCommand(CommandHeader& header)
{
  if (header.start != CMD_START || Serial.available() < header.length) {
    return false;
  }

  switch (header.code) {
    case CMD_PING:
      reply(ACK_OK);
      break;
    case CMD_MESSAGE: {
      if (header.length != sizeof(Message)) {
        discardPayload(header);
        reply(ACK_INVALID);
        break;
      }

      Message msg;
      msg.from = Serial.read();
      msg.to = Serial.read();
      msg.cmd.op = readUint16();
      msg.cmd.arg1 = readUint16();
      msg.cmd.arg2 = readUint16();
      msg.cmd.group = Serial.read();
      msg.cmd.flags = Serial.read();

      ledOn();
      if (node.sendMessage(msg)) {
        reply(ACK_OK);

        if (msg.cmd.op == CMD_OP_FLASH_IMAGE) {
          pushFlashImage(msg.to, msg.cmd.arg1);
        }
      } else {
        reply(ACK_FAIL);
      }
      ledOff();
      break;
    }
    default:
      // Don't know what this is
      discardPayload(header);
      reply(ACK_INVALID);
      break;
  }

  resetHeader(header);
  return true;
}


void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  node.setup(GATEWAY_ADDRESS, // we are the gateway
             1, // network ID
             256, // address to store settings
             ENCRYPT_KEY, // encryption key
             false); // has flash
}

void loop() {
  static CommandHeader currentHeader = { 0, };

  // First, receive any incoming messages
  Message incoming;
  if (node.receiveMessage(incoming)) {
    reply(incoming);
  }

  // Next, read and execute any commands
  if (!currentHeader.start && canReadHeader()) {
    readHeader(currentHeader);
  }

  readCommand(currentHeader);
}