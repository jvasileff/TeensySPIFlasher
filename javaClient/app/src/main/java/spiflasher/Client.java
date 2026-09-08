package spiflasher;

import com.fazecast.jSerialComm.SerialPort;
import com.fazecast.jSerialComm.SerialPortInvalidPortException;

import java.io.IOException;
import java.util.Arrays;

public class Client implements AutoCloseable {
    private static final int VERSION_MAJOR = 0;
    private static final int VERSION_MINOR = 2;

    // Programmer commands
    private static final int CMD_SCRIPT_INFO = 0;
    private static final int CMD_SPI_INFO = 1;
    private static final int CMD_SPI_READ_BLOCK = 2;
    private static final int CMD_SPI_ERASE_CHIP = 3;
    private static final int CMD_SPI_ERASE_BLOCK = 4;
    private static final int CMD_SPI_WRITE_BLOCK = 5;

    // Response codes
    private static final int REQ_SUCCESS = 0;
    private static final int REQ_FAILURE = 1;
    private static final int REQ_CMD_NOT_RECOGNIZED = 2;
    private static final int REQ_ADDR_READ_TIMEOUT = 3;
    private static final int REQ_WRITE_PROTECTED = 4;
    private static final int REQ_CHIP_ERASE_FAILURE = 5;
    private static final int REQ_PAGE_READ_TIMEOUT = 6;
    private static final int REQ_PAGE_WRITE_FAILURE = 7;

    private static final int DEFAULT_READ_TIMEOUT_MS = 5000;

    private final SerialPort serialPort;

    public Client(String port) throws IOException {
        try {
            this.serialPort = SerialPort.getCommPort(port);
        } catch (SerialPortInvalidPortException e) {
            throw new ReportableException("Invalid serial port: " + port, e);
        }

        // Configure serial port settings (the baud rate is ignored by USB CDC ports)
        serialPort.setBaudRate(115200);
        serialPort.setNumDataBits(8);
        serialPort.setNumStopBits(1);
        serialPort.setParity(SerialPort.NO_PARITY);
        serialPort.setFlowControl(SerialPort.FLOW_CONTROL_DISABLED);

        // Immediately clear any existing data in the buffers upon opening the port
        serialPort.flushIOBuffers();

        // Open the port
        if (!serialPort.openPort()) {
            throw new ReportableException("Failed to open serial port: " + port);
        }

        // Block on read for a short time to avoid expensive 1-byte reads
        setReadTimeout(DEFAULT_READ_TIMEOUT_MS);

        // Verify we can communicate with the programmer
        ping();
    }

    /**
     * Set serial port read timeout
     */
    private void setReadTimeout(int timeoutMs) {
        serialPort.setComPortTimeouts(SerialPort.TIMEOUT_READ_BLOCKING, timeoutMs, 0);
    }

    /**
     * Get the serial port read timeout
     */
    private int getReadTimeout() {
        return serialPort.getReadTimeout();
    }

    /**
     * Write a single byte to the serial port
     */
    private void writeByte(int value) throws IOException {
        writeBytes(new byte[] { (byte)(value & 0xff) }, 0, 1);
    }

    /**
     * Write multiple bytes to the serial port
     */
    private void writeBytes(byte[] bytes, int offset, int length) throws IOException {
        int bytesWritten = 0;
        while (bytesWritten < length) {
            int result = serialPort.writeBytes(
                    bytes, length - bytesWritten, offset + bytesWritten);
            if (result < 0) {
                throw new ReportableException(
                        "Error writing to the serial port (%d)".formatted(result));
            }
            if (result == 0) {
                throw new ReportableException(
                        "Unexpected zero-byte write to the serial port after %d bytes"
                                .formatted(bytesWritten));
            }
            bytesWritten += result;
        }
    }

    /**
     * Read a single byte from the serial port
     */
    private int readByte() throws IOException {
        var buffer = new byte[1];
        readBytes(1, buffer, 0);
        return buffer[0] & 0xFF;
    }

    /**
     * Read multiple bytes from the serial port
     */
    private byte[] readBytes(int count) throws IOException {
        byte[] buffer = new byte[count];
        readBytes(count, buffer, 0);
        return buffer;
    }

    private void readBytes(int count, byte[] buffer, int offset) throws IOException {
        int bytesRead = 0;
        while (bytesRead < count) {
            int result = serialPort.readBytes(
                    buffer, count - bytesRead, offset + bytesRead);
            if (result == 0) {
                throw new ReportableException("""
                     Unexpected end of stream while reading from serial port \
                     after reading %d bytes
                     """.formatted(bytesRead)
                );
            }
            if (result < 0) {
                throw new ReportableException(
                    "Error reading from the serial port (%d)".formatted(result));
            }
            bytesRead += result;
        }
    }

    private void writeFourByteAddress(int address) throws IOException {
        byte[] addressBytes = new byte[4];
        addressBytes[0] = (byte)((address >> 24) & 0xff);
        addressBytes[1] = (byte)((address >> 16) & 0xff);
        addressBytes[2] = (byte)((address >> 8) & 0xff);
        addressBytes[3] = (byte)(address & 0xff);
        writeBytes(addressBytes, 0, 4);
    }

    private void eraseBlock(Info info, int blockNumber) throws IOException {
        writeByte(CMD_SPI_ERASE_BLOCK);
        writeFourByteAddress(blockNumber * info.blockSizeBytes());
        checkResponseCode();
    }

    private void readBlock(Info info, int blockNumber, byte[] buffer, int offset)
            throws IOException {
        writeByte(CMD_SPI_READ_BLOCK);
        writeFourByteAddress(blockNumber * info.blockSizeBytes());
        checkResponseCode();

        readBytes(info.blockSizeBytes(), buffer, offset);
    }

    private void writeBlock(Info info, byte[] data, int dataOffsetBytes,
                int blockNumber) throws IOException {
        if (data.length < dataOffsetBytes + info.blockSizeBytes()) {
            throw new ReportableException("""
                Data (%d bytes) does not contain full block (%d bytes) \
                after offset (%d bytes)
                """.formatted(data.length, info.blockSizeBytes(), dataOffsetBytes)
            );
        }

        // Erase the block before writing to it
        eraseBlock(info, blockNumber);

        // Write the block's new contents
        writeByte(CMD_SPI_WRITE_BLOCK);
        writeFourByteAddress(blockNumber * info.blockSizeBytes());
        writeBytes(data, dataOffsetBytes, info.blockSizeBytes());
        checkResponseCode();
    }

    public byte[] read(Info info) throws IOException {
        var buffer = new byte[info.chipSizeBytes()];

        try {
            System.out.print("\r0 KB / 0 KB");
            for (int block = 0; block < info.blockCount(); block++) {
                readBlock(info, block, buffer, block * info.blockSizeBytes());

                System.out.print("\r%d KB / %d KB".formatted(
                    (block + 1) * info.blockSizeBytes() / 1024,
                    info.blockCount() * info.blockSizeBytes() / 1024
                ));
                System.out.flush();
            }
        } finally {
            System.out.println();
        }

        return buffer;
    }

    public void write(Info info, byte[] data) throws IOException {
        if (data.length != info.chipSizeBytes()) {
            throw new ReportableException(
                "File size (%d bytes) doesn't match chip size (%d bytes)"
                .formatted(data.length, info.chipSizeBytes())
            );
        }

        try {
            System.out.print("\r0 KB / 0 KB");
            for (int block = 0; block < info.blockCount(); block++) {
                writeBlock(info, data, block * info.blockSizeBytes(), block);
                System.out.print("\r%d KB / %d KB".formatted(
                    (block + 1) * info.blockSizeBytes() / 1024,
                    info.blockCount() * info.blockSizeBytes() / 1024
                ));
                System.out.flush();
            }
        } finally {
            System.out.println();
        }
    }

    private void verifyBlock(Info info, byte[] data, int dataOffsetBytes,
                int blockNumber) throws IOException {
        var buffer = new byte[info.blockSizeBytes()];
        readBlock(info, blockNumber, buffer, 0);
        if (!Arrays.equals(
            data, dataOffsetBytes, dataOffsetBytes + info.blockSizeBytes(),
            buffer, 0, info.blockSizeBytes())
        ) {
            throw new ReportableException(
                    "Block verification failed (block=%d)".formatted(blockNumber));
        }
    }

    public void verify(Info info, byte[] data) throws IOException {
        // Validate data size matches chip size
        if (data.length != info.chipSizeBytes()) {
            throw new ReportableException(
                "File size (%d bytes) doesn't match chip size (%d bytes)"
                .formatted(data.length, info.chipSizeBytes())
            );
        }

        try {
            System.out.print("\r0 KB / 0 KB");
            for (int block = 0; block < info.blockCount(); block++) {
                verifyBlock(info, data, block * info.blockSizeBytes(), block);

                System.out.print("\r%d KB / %d KB".formatted(
                    (block + 1) * info.blockSizeBytes() / 1024,
                    info.blockCount() * info.blockSizeBytes() / 1024
                ));
                System.out.flush();
            }
        } finally {
            System.out.println();
        }
    }

    /**
     * Check response code and throw exception if not successful
     */
    private void checkResponseCode() throws IOException {
        int responseCode = readByte();

        if (responseCode == REQ_SUCCESS) {
            return;
        }

        String errorMessage;
        switch (responseCode) {
            case REQ_FAILURE -> errorMessage = "Unexpected failure";
            case REQ_CHIP_ERASE_FAILURE -> errorMessage = "Chip erase failed";
            case REQ_PAGE_WRITE_FAILURE -> errorMessage = "Page failed to write";
            case REQ_CMD_NOT_RECOGNIZED -> {
                int command = readByte();
                errorMessage = "Command not recognized: " + command;
            }
            case REQ_ADDR_READ_TIMEOUT -> errorMessage = """
                    Programmer timed out when receiving the address bytes. Did you send \
                    the correct number of bytes?
                    """;
            case REQ_PAGE_READ_TIMEOUT -> errorMessage =
                    "Programmer timed out when receiving the block data from your PC.";
            case REQ_WRITE_PROTECTED -> errorMessage =
                    "Operation failed because NOR chip has write protection enabled.";
            default -> errorMessage =
                    "Received unknown error code: " + responseCode;
        }

        throw new RuntimeException(errorMessage);
    }

    /**
     * Record class to hold SPI chip information
     */
    public static record Info(
            int rdidManufacturer,
            int rdidMemoryType,
            int rdidCapacity,
            String manufacturerName,
            String partNumber,
            int blockCount,
            int sectorsPerBlock,
            int sectorSizeBytes,
            int addressLength,
            boolean threeByteCommands) {

        public int blockSizeBytes() {
            return sectorsPerBlock * sectorSizeBytes;
        }

        public int chipSizeBytes() {
            return blockSizeBytes() * blockCount;
        }

        public int totalSectors() {
            return sectorsPerBlock * blockCount;
        }

        /**
         * Returns a formatted string with chip information for display
         */
        public String formatInfo() {
            StringBuilder sb = new StringBuilder();
            sb.append("Chip Information\n");
            sb.append("----------------\n");
            sb.append(String.format(
                    "Manufacturer:      %s (0x%02x)\n",
                    manufacturerName, rdidManufacturer));
            sb.append(String.format(
                    "Part Number:       %s (0x%02x, 0x%02x)\n",
                    partNumber, rdidMemoryType, rdidCapacity));

            sb.append("\n");
            if ((chipSizeBytes() < 1024 * 1024) || (chipSizeBytes() % 1024 * 1024 != 0)) {
                sb.append(String.format("Size:              %d KB\n",
                        chipSizeBytes() / 1024));
            } else {
                sb.append(String.format("Size:              %d MB\n",
                        chipSizeBytes() / 1024 / 1024));
            }
            sb.append(String.format("Sector size:       %d bytes\n", sectorSizeBytes));
            sb.append(String.format("Block size:        %d bytes\n", blockSizeBytes()));
            sb.append(String.format("Sectors per block: %d\n", sectorsPerBlock));
            sb.append(String.format("Number of blocks:  %d\n", blockCount));
            sb.append(String.format("Number of sectors: %d\n", totalSectors()));

            return sb.toString();
        }
    }

    /**
     * Check if we are connected to the programmer and verify that it is running the
     * correct version
     */
    private void ping() throws IOException {
        writeByte(CMD_SCRIPT_INFO);

        int responseCode = readByte();
        int major = readByte();
        int minor = readByte();

        if (responseCode != REQ_SUCCESS) {
            throw new ReportableException("Ping failed with exit code " + responseCode);
        }

        if (major != VERSION_MAJOR || minor != VERSION_MINOR) {
            throw new ReportableException(String.format(
                "Ping failed (expected v%d.%02d, got v%d.%02d)",
                VERSION_MAJOR, VERSION_MINOR, major, minor));
        }
    }

    /**
     * Get SPI chip information
     */
    public Info getInfo() throws IOException {
        // Read SPI IDs
        writeByte(CMD_SPI_INFO);
        checkResponseCode();

        byte[] spiInfo = readBytes(3);
        int rdidManufacturer = spiInfo[0] & 0xFF;
        int rdidMemoryType = spiInfo[1] & 0xFF;
        int rdidCapacity = spiInfo[2] & 0xFF;

        // Determine chip configuration based on manufacturer and type
        String manufacturerName;
        String chipType;
        int blockCount;
        int sectorsPerBlock;
        int sectorSize;
        int addressLength;
        boolean useThreeByteCmds;

        if (rdidManufacturer == 0xC2) {
            manufacturerName = "Macronix";
            if (rdidMemoryType == 0x20 && rdidCapacity == 0x19) {
                chipType = "MX25L25635F";
                blockCount = 512;
                sectorsPerBlock = 16;
                sectorSize = 0x1000;
                addressLength = 4;
                useThreeByteCmds = false;
            } else {
                throw new ReportableException(String.format(
                        "Unknown Macronix chip type (0x%02x, 0x%02x)",
                        rdidMemoryType, rdidCapacity));
            }
        } else if (rdidManufacturer == 0x01) {
            manufacturerName = "Spansion/Cypress";
            if (rdidMemoryType == 0x60 && rdidCapacity == 0x19) {
                chipType = "S25FL256L";
                blockCount = 512;
                sectorsPerBlock = 16;
                sectorSize = 0x1000;
                addressLength = 4;
                useThreeByteCmds = false;
            } else {
                throw new ReportableException(String.format(
                        "Unknown Spansion/Cypress chip type (0x%02x, 0x%02x)",
                        rdidMemoryType, rdidCapacity));
            }
        } else {
            throw new ReportableException(String.format(
                    "Unknown chip manufacturer (0x%02x)",
                    rdidManufacturer));
        }

        return new Info(
            rdidManufacturer,
            rdidMemoryType,
            rdidCapacity,
            manufacturerName,
            chipType,
            blockCount,
            sectorsPerBlock,
            sectorSize,
            addressLength,
            useThreeByteCmds
        );
    }

    public void eraseChip() throws IOException {
        int oldTimeout = getReadTimeout();
        try {
            setReadTimeout(0); // very long operation; disable read timeout
            writeByte(CMD_SPI_ERASE_CHIP);
            checkResponseCode();
        } finally {
            setReadTimeout(oldTimeout);
        }
    }

    /**
     * Close the serial connection
     */
    public void close() {
        if (serialPort != null && serialPort.isOpen()) {
            serialPort.closePort();
        }
    }
}