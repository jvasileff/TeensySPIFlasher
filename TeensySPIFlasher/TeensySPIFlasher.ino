#include <Arduino.h>
#include <SPI.h>

/*********************\
| Board configuration |
\*********************/
// Pin assignments per board. The chip select is driven manually with digitalWrite,
// so it does not need to be the SPI peripheral's hardware CS pin.
#if defined(ARDUINO_TEENSY40) || defined(ARDUINO_TEENSY41)
  // The SPI library uses these pins by default on Teensy 4.0 and 4.1:
  // - SS    Pin 10    CS# / Chip select
  // - MOSI  Pin 11    SI/SIO0 / Controller Out Peripheral In
  // - MISO  Pin 12    SO/SIO1 / Controller In Peripheral Out
  // - SCK   Pin 13    SCLK / Clock
  #define CHIP_SELECT   SS
  #define WRITE_PROTECT 14  // WP#/SIO2
  #define RESET         15  // RESET#/SIO3
#elif defined(ARDUINO_WAVESHARE_RP2040_ZERO)
  // The RP2040-Zero does not expose GP16-GP19, the default SPI0 pins, so use
  // the GP0-GP3 SPI0 group instead (GP4-GP7 is the other valid group).
  #define SPI_PIN_MISO  0   // SO/SIO1 / Controller In Peripheral Out
  #define CHIP_SELECT   1   // CS# / Chip select
  #define SPI_PIN_SCK   2   // SCLK / Clock
  #define SPI_PIN_MOSI  3   // SI/SIO0 / Controller Out Peripheral In
  #define WRITE_PROTECT 4   // WP#/SIO2
  #define RESET         5   // RESET#/SIO3
#else
  #error "Unsupported board: add a pin mapping for it above"
#endif


/****************************\
| Flash memory configuration |
\****************************/

// I am using an MX25L25635F and got these values from the datasheet
#define CHIP_READ_SPEED       32000000  // I found 32MHz to be stable for reads and 16MHz for writes,
#define CHIP_WRITE_SPEED      16000000  // but this'll vary from person to person
#define DATA_ORDER            MSBFIRST
#define DATA_MODE             SPI_MODE0 

// These probably shouldn't be hardcoded
#define SPI_PAGE_SIZE   256
#define SPI_BLOCK_SIZE  0x10000  // 64KB


/***************\
| SPI Constants |
\***************/

// The command IDs can also be found in the datasheet
#define SPI_COMMAND_WRDI      0x04  // Disable writes
#define SPI_COMMAND_RDSR      0x05  // Read status register
#define SPI_COMMAND_WREN      0x06  // Enable 
#define SPI_COMMAND_PP4B      0x12  // Program page by 4-byte address
#define SPI_COMMAND_READ4B    0x13  // Read data bytes, starting from 4-byte address
#define SPI_COMMAND_RDSCUR    0x2B  // Read security register
#define SPI_COMMAND_CE        0x60  // Chip erase
#define SPI_COMMAND_RDID      0x9F  // Read ID (JEDEC Manufacturer ID)
#define SPI_COMMAND_BE4B      0xDC  // Erase 64KB block by 4-byte address

// Status registers. Taken directly from SPIway.c
#define SPI_STATUS_WIP 				0b00000001  // Write in Progress bit set
#define SPI_STATUS_WEL	 			0b00000010  // Write enable bit set
#define SPI_STATUS_BP0	 			0b00000100
#define SPI_STATUS_BP1			 	0b00001000
#define SPI_STATUS_BP2			 	0b00010000
#define SPI_STATUS_BP3			 	0b00100000
#define SPI_STATUS_QE			 	  0b01000000  // Quad enable bit set
#define SPI_STATUS_SRWD			 	0b10000000  // Status register write disable set

// Security status registers (only available on some chips)
#define SPI_SECURITY_OTP			0b00000001  // Factory lock bit set
#define SPI_SECURITY_LDSO 		0b00000010  // Lock-down bit set (cannot program/erase otp)
#define SPI_SECURITY_PSB 			0b00000100  // Program suspended bit set
#define SPI_SECURITY_ESB		 	0b00001000  // Erase suspended bit set
#define SPI_SECURITY_RESERVED	0b00010000
#define SPI_SECURITY_P_FAIL		0b00100000  // Program operation failed bit set
#define SPI_SECURITY_E_FAIL		0b01000000  // Erase operation failed bit set
#define SPI_SECURITY_WPSEL		0b10000000  // Status register write disable set


/*************************\
| Python script constants |
\*************************/

// Some constants for interacting with our Python script
#define VERSION_MAJOR		((uint8_t) 0)
#define VERSION_MINOR		((uint8_t) 2)

// Command IDs
#define CMD_SCRIPT_INFO     0
#define CMD_SPI_INFO        1
#define CMD_SPI_READ_BLOCK  2
#define CMD_SPI_ERASE_CHIP  3
#define CMD_SPI_ERASE_BLOCK 4
#define CMD_SPI_WRITE_BLOCK 5

// Response codes
#define REQ_SUCCESS             ((uint8_t) 0)
#define REQ_FAILURE             ((uint8_t) 1)
#define REQ_CMD_NOT_RECONGIZED  ((uint8_t) 2)
#define REQ_ADDR_READ_TIMEOUT   ((uint8_t) 3)
#define REQ_WRITE_PROTECTED     ((uint8_t) 4)
#define REQ_CHIP_ERASE_FAILURE  ((uint8_t) 5)
#define REQ_PAGE_READ_TIMEOUT   ((uint8_t) 6)
#define REQ_PAGE_WRITE_FAILURE  ((uint8_t) 7)

// Chip configuration
bool hasSecurityRegister = false;

// Reused values, buffers, etc.
#define ADDRESS_BUFFER_SIZE 4
uint8_t addressBuffer[ADDRESS_BUFFER_SIZE];

#define DATA_BUFFER_SIZE 4096
uint8_t dataBuffer[DATA_BUFFER_SIZE];

// blocks for up to the Serial timeout
int blockingSerialRead() {
  uint8_t result;
  int bytesRead = Serial.readBytes((char*)&result, 1);
  if (bytesRead < 1) {
    return -1;
  }
  return result;
}

// Reads the 4-byte address from the serial connection. Returns false if this failed
bool readAddress() {
  memset(addressBuffer, 0, sizeof(addressBuffer));
  int bytesRead = Serial.readBytes((char*)addressBuffer, ADDRESS_BUFFER_SIZE);
  if (bytesRead < ADDRESS_BUFFER_SIZE) {
    Serial.write(REQ_ADDR_READ_TIMEOUT);
    Serial.flush();
    return false;
  }
  return true;
}

void sendScriptInfo() {
  Serial.write(REQ_SUCCESS);
  Serial.write(VERSION_MAJOR);
  Serial.write(VERSION_MINOR);
  Serial.flush();
}

void sendSpiInfo() {
  // Get SPI info
  SPI.beginTransaction(SPISettings(CHIP_READ_SPEED, DATA_ORDER, DATA_MODE));
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer((uint8_t) SPI_COMMAND_RDID);
  uint8_t manufacturerId = SPI.transfer(0);
  uint8_t memoryType = SPI.transfer(0);
  uint8_t capacityCode = SPI.transfer(0);
  digitalWrite(CHIP_SELECT, HIGH);
  SPI.endTransaction();

  // Update local chip configuration info
  if (manufacturerId == 0xC2) { // Macronix
    hasSecurityRegister = true;
  } else if (manufacturerId == 0x01) { // Spansion
    hasSecurityRegister = false;
  } else {
    hasSecurityRegister = false; // Default for unknown chips
  }

  // Send response
  Serial.write(REQ_SUCCESS);
  Serial.write(manufacturerId);
  Serial.write(memoryType);
  Serial.write(capacityCode);
  Serial.flush();
}

void readBlock() {
  if (!readAddress()) {
    return;
  }
  Serial.write(REQ_SUCCESS);

  SPI.beginTransaction(SPISettings(CHIP_READ_SPEED, DATA_ORDER, DATA_MODE));
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer(SPI_COMMAND_READ4B);
  for (uint8_t i = 0; i < ADDRESS_BUFFER_SIZE; i++) {
    SPI.transfer(addressBuffer[i]);
  }

  // Read entire block, one byte at a time
  for (uint32_t chunk = 0; chunk < (SPI_BLOCK_SIZE / DATA_BUFFER_SIZE); chunk++) {
    for (uint32_t i = 0; i < DATA_BUFFER_SIZE; i++) {
      dataBuffer[i] = SPI.transfer(0);
    }
    Serial.write(dataBuffer, DATA_BUFFER_SIZE);
  }
  digitalWrite(CHIP_SELECT, HIGH);
  SPI.endTransaction();
  Serial.flush();
}

uint8_t getStatus() {
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer(SPI_COMMAND_RDSR);
  uint8_t status = SPI.transfer(0);
  digitalWrite(CHIP_SELECT, HIGH);
  return status;
}

uint8_t getSecurityStatus() {
  if (!hasSecurityRegister) {
    return 0;
  }

  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer(SPI_COMMAND_RDSCUR);
  uint8_t securityStatus = SPI.transfer(0);
  digitalWrite(CHIP_SELECT, HIGH);
  return securityStatus;
}

void busyWaitForWriteToComplete() {
    digitalWrite(CHIP_SELECT, LOW);
    SPI.transfer(SPI_COMMAND_RDSR);

    // The chip now sends the status register every cycle, so keep checking until WIP is clear
    while ((SPI.transfer(0) & SPI_STATUS_WIP) != 0);

    // Writing is complete
    digitalWrite(CHIP_SELECT, HIGH);
}

bool isWriteFlagEnabled() {
    return (getStatus() & SPI_STATUS_WEL) != 0;
}

void setWriteEnableFlag() {
  SPI.beginTransaction(SPISettings(CHIP_WRITE_SPEED, DATA_ORDER, DATA_MODE));
  
  // Set the write flag and check that the status has been updated
  do {
    digitalWrite(CHIP_SELECT, LOW);
    SPI.transfer(SPI_COMMAND_WREN);
    digitalWrite(CHIP_SELECT, HIGH);
  } while(!isWriteFlagEnabled());

  SPI.endTransaction();
}

void eraseChip() {
  // Disable write protection and enable writes
  digitalWrite(WRITE_PROTECT, HIGH);
  setWriteEnableFlag();
  
  // Send command to erase the chip
  SPI.beginTransaction(SPISettings(CHIP_WRITE_SPEED, DATA_ORDER, DATA_MODE));
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer(SPI_COMMAND_CE);
  digitalWrite(CHIP_SELECT, HIGH);

  busyWaitForWriteToComplete();

  // Re-enable write protection (write flag was reset when above command completed)
  digitalWrite(WRITE_PROTECT, LOW);

  // If any block protect bits are set or the status register is write-protected, assume this failed
  uint8_t status = getStatus();
  uint8_t securityStatus = getSecurityStatus();
  if ((status & (SPI_STATUS_BP0 | SPI_STATUS_BP1 | SPI_STATUS_BP2 | SPI_STATUS_BP3 | SPI_STATUS_SRWD)) != 0) {
    Serial.write(REQ_WRITE_PROTECTED);
  }
  // Some chips (like mine) have a security register as well
  else if ((securityStatus & (SPI_SECURITY_E_FAIL | SPI_SECURITY_WPSEL)) != 0) {
    if ((securityStatus & SPI_SECURITY_E_FAIL) != 0) {
      Serial.write(REQ_CHIP_ERASE_FAILURE);
    } else if ((securityStatus & SPI_SECURITY_WPSEL) != 0) {
      Serial.write(REQ_WRITE_PROTECTED);
    }
  }
  else {
    Serial.write(REQ_SUCCESS);
  }
  
  SPI.endTransaction();
  Serial.flush();
}


// Erases an entire block
void eraseBlock() {
  if (!readAddress()) {
    return;
  }
  
  // Disable write protection and enable writes
  digitalWrite(WRITE_PROTECT, HIGH);
  setWriteEnableFlag();
  
  // Send command to erase the chip
  SPI.beginTransaction(SPISettings(CHIP_WRITE_SPEED, DATA_ORDER, DATA_MODE));
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer(SPI_COMMAND_BE4B);
  for (uint8_t i = 0; i < ADDRESS_BUFFER_SIZE; i++) {
    SPI.transfer(addressBuffer[i]);
  }
  digitalWrite(CHIP_SELECT, HIGH);

  busyWaitForWriteToComplete();

  // Re-enable write protection (write flag was reset when above command completed)
  digitalWrite(WRITE_PROTECT, LOW);

  // If any block protect bits are set or the status register is write-protected, assume this failed
  uint8_t status = getStatus();
  uint8_t securityStatus = getSecurityStatus();
  if ((status & (SPI_STATUS_BP0 | SPI_STATUS_BP1 | SPI_STATUS_BP2 | SPI_STATUS_BP3 | SPI_STATUS_SRWD)) != 0) {
    Serial.write(REQ_WRITE_PROTECTED);
  }
  // Some chips (like mine) have a security register as well
  else if ((securityStatus & (SPI_SECURITY_E_FAIL | SPI_SECURITY_WPSEL)) != 0) {
    if ((securityStatus & SPI_SECURITY_E_FAIL) != 0) {
      Serial.write(REQ_CHIP_ERASE_FAILURE);
    } else if ((securityStatus & SPI_SECURITY_WPSEL) != 0) {
      Serial.write(REQ_WRITE_PROTECTED);
    }
  }
  else {
    Serial.write(REQ_SUCCESS);
  }
  
  SPI.endTransaction();
  Serial.flush();
}

// Writes an entire block, one page at a time (takes a very long time)
void writeBlock() {
  if (!readAddress()) {
    return;
  }

  bool failedToReadPage = false;
  bool writeProtectionEnabled = false;
  bool failedToWritePage = false;

  // Disable write protection
  digitalWrite(WRITE_PROTECT, HIGH);

  for (uint32_t page = 0; page < (SPI_BLOCK_SIZE / SPI_PAGE_SIZE); page++) {
    // Enable write flag for each request
    setWriteEnableFlag();

    SPI.beginTransaction(SPISettings(CHIP_WRITE_SPEED, DATA_ORDER, DATA_MODE));
    digitalWrite(CHIP_SELECT, LOW);
    SPI.transfer(SPI_COMMAND_PP4B);
    // Some address trickery, courtesy of SPIway.c
    SPI.transfer(addressBuffer[0]);
    SPI.transfer(addressBuffer[1]);
    SPI.transfer(addressBuffer[2] | page);
    SPI.transfer(addressBuffer[3]);

    // Send page data
    for (uint32_t i = 0; i < SPI_PAGE_SIZE; i++) {
      // Don't use Serial.read(), because bytes from sender may not be
      // immediately available
      int value = blockingSerialRead();
      if (value == -1) {
        failedToReadPage = true;
        break;
      }
      SPI.transfer(value);
    }
    digitalWrite(CHIP_SELECT, HIGH);

    if (failedToReadPage) {
      SPI.endTransaction();
      break;
    }

    busyWaitForWriteToComplete();

    // If any block protect bits are set or the status register is write-protected, assume this failed
    if ((getStatus() & (SPI_STATUS_BP0 | SPI_STATUS_BP1 | SPI_STATUS_BP2 | SPI_STATUS_BP3 | SPI_STATUS_SRWD)) != 0) {
      writeProtectionEnabled = true;
      SPI.endTransaction();
      break;
    }
    // Some chips (like mine) have other registers we can check
    else if (hasSecurityRegister) {
      uint8_t securityStatus = getSecurityStatus();
      
      if ((securityStatus & SPI_SECURITY_P_FAIL) != 0) {
        failedToWritePage = true;
        SPI.endTransaction();
        break;
      } else if ((securityStatus & SPI_SECURITY_WPSEL) != 0) {
        writeProtectionEnabled = true;
        SPI.endTransaction();
        break;
      }
    }
  
    SPI.endTransaction();
  }
  
  // Re-enable write protection
  digitalWrite(WRITE_PROTECT, LOW);

  if (failedToReadPage) {
    Serial.write(REQ_PAGE_READ_TIMEOUT);
  } else if (failedToWritePage) {
    Serial.write(REQ_PAGE_WRITE_FAILURE);
  } else if (writeProtectionEnabled) {
    Serial.write(REQ_WRITE_PROTECTED);
  } else {
    Serial.write(REQ_SUCCESS);
  }

  Serial.flush();
}

void commandNotRecognized(uint8_t commandByte) {
  Serial.write(REQ_CMD_NOT_RECONGIZED);
  Serial.write(commandByte);
  Serial.flush();
}

void setup() {
  // Initialize serial and SPI comms
  Serial.begin(9600);   // Ignored: all supported boards present a USB CDC port, not a UART
#if defined(ARDUINO_ARCH_RP2040)
  // Route SPI0 to the pins chosen above before starting the peripheral
  SPI.setRX(SPI_PIN_MISO);
  SPI.setCS(CHIP_SELECT);
  SPI.setSCK(SPI_PIN_SCK);
  SPI.setTX(SPI_PIN_MOSI);
#endif
  SPI.begin();

  // Configure the pins
  pinMode(CHIP_SELECT, OUTPUT);
  pinMode(WRITE_PROTECT, OUTPUT);
  pinMode(RESET, OUTPUT);

  // Reset must be held high
  digitalWrite(WRITE_PROTECT, LOW);
  digitalWrite(RESET, HIGH);

  // Wait a few seconds
  delay(2000);
}

void loop() {
  if (Serial.available()) {
    uint8_t commandByte = Serial.read();

    switch(commandByte) {
      case CMD_SCRIPT_INFO:
        sendScriptInfo();
        break;
      case CMD_SPI_INFO:
        sendSpiInfo();
        break;
      case CMD_SPI_READ_BLOCK:
        readBlock();
        break;
      case CMD_SPI_ERASE_CHIP:
        eraseChip();
        break;
      case CMD_SPI_ERASE_BLOCK:
        eraseBlock();
        break;
      case CMD_SPI_WRITE_BLOCK:
        writeBlock();
        break;
      default:
        commandNotRecognized(commandByte);
        break;
    }
  }
}
